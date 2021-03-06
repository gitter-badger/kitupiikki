/*
   Copyright (C) 2017 Arto Hyvättinen

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <cmath>
#include <QSqlQuery>
#include <QTemporaryFile>
#include <QPrinter>

#include <QDebug>
#include <QSqlError>

#include "laskumodel.h"
#include "laskutusverodelegaatti.h"
#include "db/kirjanpito.h"
#include "db/tilinvalintadialogi.h"
#include "kirjaus/verodialogi.h"
#include "laskuntulostaja.h"

LaskuModel::LaskuModel(QObject *parent, AvoinLasku hyvitettava) :
    QAbstractTableModel( parent ),
    hyvitettavaLasku_(hyvitettava)
{

}

int LaskuModel::rowCount(const QModelIndex & /* parent */) const
{
    return rivit_.count();
}

int LaskuModel::columnCount(const QModelIndex & /* parent */) const
{
    return 8;
}

QVariant LaskuModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if( role == Qt::TextAlignmentRole)
        return QVariant( Qt::AlignCenter | Qt::AlignVCenter);
    else if( role != Qt::DisplayRole )
        return QVariant();
    else if( orientation == Qt::Horizontal)
    {
        switch (section)
        {
            case NIMIKE:
                return tr("Nimike");
            case MAARA:
                return tr("Määrä");
            case YKSIKKO:
                return tr("Yksikkö");
            case AHINTA :
                return tr("á netto");
            case ALV:
                return tr("Alv");
            case TILI:
                return tr("Tili");
            case KOHDENNUS:
                return tr("Kohdennus");
            case BRUTTOSUMMA:
                return tr("Yhteensä");
        }

    }
    return QVariant( section + 1);
}

QVariant LaskuModel::data(const QModelIndex &index, int role) const
{
    if( !index.isValid())
        return QVariant();

    LaskuRivi rivi = rivit_.value(index.row());

    if( role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch (index.column()) {
        case NIMIKE:
            return rivi.nimike;
        case MAARA:
            if( role == Qt::DisplayRole)
                return QString("%L1").arg(rivi.maara,0,'f',2);
            else
                return rivi.maara;
        case YKSIKKO:
            return rivi.yksikko;
        case AHINTA:
            if( role == Qt::DisplayRole)
                return QString("%L1 €").arg(rivi.ahintaSnt / 100.0,0,'f',2);
            else
                return rivi.ahintaSnt;
        case ALV:
            switch (rivi.alvKoodi) {
            case AlvKoodi::EIALV :
                return QVariant();
            case AlvKoodi::ALV0:
                return tr("Veroton myynti");
            case AlvKoodi::RAKENNUSPALVELU_MYYNTI:
                return tr("AVL 8 c §");
            case AlvKoodi::YHTEISOMYYNTI_PALVELUT:
            case AlvKoodi::YHTEISOMYYNTI_TAVARAT:
                return tr("AVL 72 a §");
            default:
                return QVariant( QString("%1 %").arg(rivi.alvProsentti));
            }
        case KOHDENNUS:
            if( role == Qt::DisplayRole)
            {
                if( rivi.kohdennus.tyyppi() != Kohdennus::EIKOHDENNETA)
                    return rivi.kohdennus.nimi();
            }
            else if( role == Qt::EditRole)
                return rivi.kohdennus.id();
            return QVariant();
        case TILI:
            if( rivi.myyntiTili.numero())
                return QVariant( QString("%1 %2").arg(rivi.myyntiTili.numero()).arg(rivi.myyntiTili.nimi()) );
            return QVariant();
        case BRUTTOSUMMA:
            if( role == Qt::DisplayRole)
                return QString("%L1 €").arg(rivi.yhteensaSnt() / 100.0,0,'f',2);
            else
                return rivi.yhteensaSnt();
        }
    }
    else if( role == NimikeRooli)
        return rivi.nimike;
    else if( role == AlvKoodiRooli)
        return rivi.alvKoodi;
    else if( role == AlvProsenttiRooli)
        return rivi.alvProsentti;
    else if( role == NettoRooli)
    {
        return rivi.maara * rivi.ahintaSnt;
    }
    else if( role == VeroRooli)
    {
        return rivi.yhteensaSnt() - rivi.maara * rivi.ahintaSnt;
    }
    else if( role == TuoteKoodiRooli)
        return rivi.tuoteKoodi;
    else if( role == Qt::TextAlignmentRole)
    {
        if( index.column()==BRUTTOSUMMA || index.column() == MAARA || index.column() == ALV || index.column() == AHINTA)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        else
            return QVariant( Qt::AlignLeft | Qt::AlignVCenter);

    }
    else if( role == Qt::DecorationRole && index.column() == ALV)
    {
        return kp()->alvTyypit()->kuvakeKoodilla( rivi.alvKoodi % 100 );
    }

    return QVariant();
}

bool LaskuModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    int rivi = index.row();

    if( role == Qt::EditRole)
    {
        switch (index.column()) {
        case NIMIKE:
            rivit_[rivi].nimike = value.toString();
            return true;
        case MAARA:
            rivit_[rivi].maara = value.toDouble();
            // Uusi summa
            paivitaSumma(rivi);
            return true;

        case AHINTA:
            rivit_[rivi].ahintaSnt = value.toInt();
            if( value.toInt())
            {
                // Lisätään loppuun aina automaattisesti uusi rivi
                // kun tähän on syötetty kelpo summa
                if( rivi == rivit_.count() - 1)
                    lisaaRivi();
                // Uusi summa
                paivitaSumma(rivi);
            }
            return true;
        case YKSIKKO:
            rivit_[rivi].yksikko = value.toString();
            return true;
        case KOHDENNUS:
            rivit_[rivi].kohdennus = kp()->kohdennukset()->kohdennus(value.toInt());
            return true;

        case TILI:
        {
            // Tili asetetaan numerolla!
            Tili uusitili;
            if( value.toInt() )
                uusitili = kp()->tilit()->tiliNumerolla( value.toInt());
            else if(!value.toString().isEmpty() && value.toString() != " " && value.toString() != "0")
                uusitili = TilinValintaDialogi::valitseTili(value.toString());
            else if( value.toString()==" " || !rivit_[rivi].myyntiTili.onkoValidi())
                uusitili = TilinValintaDialogi::valitseTili( QString());

            if( uusitili.onkoValidi())
                rivit_[rivi].myyntiTili = uusitili;
            return true;
        }
        case BRUTTOSUMMA:
            // Lasketaan bruton avulla nettoyksikköhinta ja laitetaan se paikalleen
            if( !rivit_[rivi].maara)
                return false;

            int alvprosentti = rivit_[rivi].alvProsentti;
            double netto =  100.0 * value.toInt() / rivit_[rivi].maara / ( 100.0 + alvprosentti) ;
            rivit_[rivi].ahintaSnt = netto;
            emit dataChanged( createIndex(rivi, AHINTA , rivi), createIndex(rivi, AHINTA, rivi) );
            paivitaSumma(rivi);

            // Lisätään loppuun aina automaattisesti uusi rivi
            // kun tähän on syötetty kelpo summa
            if( rivi == rivit_.count() - 1)
                lisaaRivi();
            return true;
        }
    }
    else if( role == AlvKoodiRooli)
    {
        if( value.toInt() == LaskutusVeroDelegaatti::MUUVEROVALINTA)
        {
            // Jos delegaatista valitaan muu, näytetään dialogi
            VeroDialogi veroDlg;
            if( veroDlg.exec(rivit_[rivi].alvKoodi, rivit_[rivi].alvProsentti, true) )
            {
                rivit_[rivi].alvKoodi = veroDlg.alvKoodi();
                rivit_[rivi].alvProsentti = veroDlg.alvProsentti();
            }
        }
        else
            rivit_[rivi].alvKoodi = value.toInt();
        return true;
    }
    else if( role == AlvProsenttiRooli)
    {
        rivit_[rivi].alvProsentti = value.toInt();
        paivitaSumma(rivi);
        return true;
    }
    else if( role == TuoteKoodiRooli)
    {
        rivit_[rivi].tuoteKoodi = value.toInt();
    }

    return false;
}

Qt::ItemFlags LaskuModel::flags(const QModelIndex &index) const
{
    return QAbstractTableModel::flags(index) | Qt::ItemIsEditable;

}

int LaskuModel::laskunSumma() const
{
    int summa = 0;
    foreach (LaskuRivi rivi, rivit_)
    {
        summa += std::round(rivi.yhteensaSnt());
    }
    return summa;
}

LaskuRivi LaskuModel::rivi(int indeksi) const
{
    return rivit_.value(indeksi);
}

QDate LaskuModel::pvm() const
{
    if( kirjausperuste()==SUORITEPERUSTE)
        return toimituspaiva();
    else if(kirjausperuste()==LASKUTUSPERUSTE || kirjausperuste()==KATEISLASKU)
        return kp()->paivamaara();
    else
        return QDate(); // Ei saada päivämäärää (Maksuperuste)
}

qulonglong LaskuModel::laskunro() const
{
    qlonglong pohjanro = kp()->asetukset()->isoluku("LaskuSeuraavaId") / 10;
    if( pohjanro < 100)
        pohjanro = 100;

    while(true)
    {
        // Lasketaan aina tunnistenumero uudelleen!!!
        qlonglong numero = pohjanro * 10 + laskeViiteTarkiste(pohjanro);
        // Varmistetaan, että tämä numero ei vielä ole käytössä!
        QSqlQuery kysely(QString("SELECT id FROM lasku WHERE id=%1").arg(numero));
        if( !kysely.next())
            return numero;
        pohjanro++;
    }
}

QString LaskuModel::viitenumero() const
{
    // Muotoilee viitenumeron tulosteasun
    QString str = QString::number( laskunro() );
    for(int i=0; i< str.count() / 5; i++)
    {
        str.insert(i * 5 + i,' ');
    }
    return str;
}

bool LaskuModel::tallenna(Tili rahatili)
{
    // Ensin tehdään tosite
    TositeModel tosite( kp()->tietokanta() );    
    if( hyvityslasku().viitenro)
        tosite.asetaOtsikko( tr("%1 [Hyvityslasku %2]").arg(laskunsaajanNimi()).arg(laskunro()) );
    else
        tosite.asetaOtsikko( tr("%1 [%2]").arg(laskunsaajanNimi()).arg(laskunro()) );
    tosite.asetaKommentti( lisatieto() );
    tosite.asetaTositelaji( kp()->asetukset()->luku("LaskuTositelaji", 1) );
    tosite.json()->set("Lasku", laskunro());

    tosite.asetaPvm(pvm() );

    if(  hyvityslasku().tosite > 0 && kirjausperuste() == MAKSUPERUSTE )
    {
        // Maksuperusteinen kirjataan samaan tositteeseen alkuperäisen laskun kanssa
        tosite.lataa( hyvityslasku().tosite );
        // Lisätään hyvityslaskun kommentit
        tosite.asetaKommentti( tosite.kommentti() + "\n\n[Hyvityslasku]\n" + lisatieto() );
    }

    VientiModel *viennit = tosite.vientiModel();
    foreach (LaskuRivi rivi, rivit_)
    {
        VientiRivi vienti;
        vienti.selite = rivi.nimike;
        vienti.pvm = pvm();
        vienti.tili = rivi.myyntiTili;
        vienti.kohdennus = rivi.kohdennus;
        vienti.alvkoodi = rivi.alvKoodi;
        vienti.alvprosentti = rivi.alvProsentti;

        int nettoSnt = std::round( rivi.ahintaSnt * rivi.maara );
        int bruttoSnt = std::round( rivi.yhteensaSnt() );
        int veroSnt = bruttoSnt - nettoSnt;

        vienti.json.set("Maara", QString("%L1").arg(rivi.maara,0,'f',2));
        vienti.json.set("Yksikko", rivi.yksikko);
        vienti.json.set("Tuote", rivi.tuoteKoodi);


        VientiRivi verorivi;
        // Nettokirjauksessa kirjataan alv erikseen
        if( rivi.alvKoodi == AlvKoodi::MYYNNIT_NETTO)
        {
            verorivi.pvm = pvm();
            verorivi.selite = rivi.nimike;
            verorivi.tili = kp()->tilit()->tiliTyypilla(TiliLaji::ALVVELKA);
            verorivi.alvkoodi = AlvKoodi::ALVKIRJAUS + rivi.alvKoodi;
            verorivi.alvprosentti = rivi.alvProsentti;

            if( nettoSnt > 0)
            {
                vienti.kreditSnt = nettoSnt;
                verorivi.kreditSnt = veroSnt;
            }
            else
            {
                vienti.debetSnt = 0 - nettoSnt;
                verorivi.debetSnt = 0 - veroSnt;
            }
        }
        else
        {
            if( nettoSnt > 0)
                vienti.kreditSnt = bruttoSnt;
            else
                vienti.debetSnt = 0 - bruttoSnt;
        }
        viennit->lisaaVienti(vienti);
        if( verorivi.tili.onkoValidi())
            viennit->lisaaVienti(verorivi);
    }
    // Vielä rahasummarivi !!!
    if( kirjausperuste() != MAKSUPERUSTE)
    {
        VientiRivi raharivi;
        raharivi.tili = rahatili;
        raharivi.pvm = pvm();
        raharivi.selite = tr("%1 [%2]").arg(laskunsaajanNimi()).arg(laskunro());

        if( laskunSumma() > 0 )
            raharivi.debetSnt = laskunSumma();
        else
            raharivi.kreditSnt = 0 - laskunSumma();

        if( hyvityslasku().viitenro )
            raharivi.eraId = hyvityslasku().json.luku("TaseEra");

        viennit->lisaaVienti(raharivi);
    }

    // Tallennetaan liiteeksi

    // Luo tilapäisen pdf-tiedoston
    QTemporaryFile *file = new QTemporaryFile(QDir::tempPath() + "/lasku-XXXXXX.pdf", this);
    file->open();
    file->close();

    QPrinter printer;
    printer.setPaperSize(QPrinter::A4);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName( file->fileName() );

    LaskunTulostaja tulostaja(this);
    tulostaja.tulosta(&printer);

    tosite.liiteModel()->lisaaTiedosto( file->fileName(), tr("Lasku nr %1").arg(laskunro()));
    tosite.tallenna();

    
    QSqlQuery query;
    query.prepare("INSERT INTO lasku(id,tosite,laskupvm,erapvm,summaSnt,avoinSnt,asiakas,kirjausperuste,json) "
                  "VALUES(:id,:tosite,:pvm,:erapvm,:summa,:avoin,:asiakas,:kirjausperuste,:json)");
    query.bindValue(":id", laskunro());
    query.bindValue(":tosite", tosite.id());
    query.bindValue(":pvm", kp()->paivamaara());
    query.bindValue(":erapvm", erapaiva());
    query.bindValue(":summa", laskunSumma());

    if( kirjausperuste() == KATEISLASKU || hyvityslasku().viitenro)
        query.bindValue(":avoin", 0);
    else
        query.bindValue(":avoin", laskunSumma());
    query.bindValue(":asiakas", laskunsaajanNimi());
    query.bindValue(":kirjausperuste", kirjausperuste());

    JsonKentta json;
    json.set("Osoite", osoite());
    json.set("Toimituspvm", toimituspaiva());
    json.set("Lisatieto", lisatieto());
    json.set("Email", email());

    if( hyvityslasku().viitenro )
        json.set("Hyvityslasku", hyvityslasku().viitenro);

    // Etsitään tase-erä. Tämän tallentaminen laskun json-kenttään helpottaa maksun suorittamista ;)
    if( (kirjausperuste() == SUORITEPERUSTE || kirjausperuste() == LASKUTUSPERUSTE) && !hyvityslasku().viitenro )
    {
        for( int i=0; i < viennit->rowCount(QModelIndex()); i++)
        {
            if( viennit->index(i, 0).data(VientiModel::TiliNumeroRooli).toInt() == rahatili.numero())
            {
                json.set("TaseEra", viennit->index(i,0).data(VientiModel::IdRooli).toInt());
                break;
            }
        }
        json.set("Saatavatili", rahatili.numero());
    }

    // Etsitään liitetiedoston nimi. Näin liitetiedosto helpommin viitattavissa myös hyvityslaskuille
    for(int i=0; i < tosite.liiteModel()->rowCount(QModelIndex()); i++)
    {
        if( tosite.liiteModel()->index(i,0).data(LiiteModel::OtsikkoRooli) == tr("Lasku nr %1").arg(laskunro()) )
        {
            json.set("Liite", tosite.liiteModel()->index(i,0).data(LiiteModel::TiedostoNimiRooli).toString());
        }
    }


    query.bindValue(":json", json.toSqlJson() );
    qDebug()  << query.exec();

    qDebug() << query.lastError().text();
    qDebug() << query.lastQuery();

    if( hyvityslasku().viitenro)
    {
        // Vähennetään hyvityslasku alkuperäisen laskun avoimista
        query.exec( QString("UPDATE lasku SET avoinSnt = avoinSnt - %1 where id = %2")
                    .arg( 0 - laskunSumma()).arg( hyvityslasku().viitenro) );
    }

    // Kelataan laskuria eteenpäin - tallennettava laskunnumero
    kp()->asetukset()->aseta("LaskuSeuraavaId", laskunro() );

    return true;
}

int LaskuModel::laskeViiteTarkiste(qulonglong luvusta)
{
    int indeksi = 0;
    int summa = 0;

    while( luvusta > 0)
    {
        int tarkasteltava = luvusta % 10;
        luvusta = luvusta / 10;
        if( indeksi % 3 == 0)
            summa += 7 * tarkasteltava;
        else if( indeksi % 3 == 1)
            summa += 3 * tarkasteltava;
        else
            summa += tarkasteltava;
        indeksi++;
    }
    return ( 10 - summa % 10) % 10;

}

void LaskuModel::lisaaRivi(LaskuRivi rivi)
{
    int rivia = rivit_.count();
    if( rivia  && rivit_.value(rivia-1).ahintaSnt == 0)
    {
        // Jos viimeisenä on tyhjä rivi, korvataan se tällä ja lisätään sen jälkeen uusi
        rivit_[rivia-1] = rivi;
        emit dataChanged( index(rivia-1, 0), index(rivia-1, columnCount(QModelIndex())) );
        rivi = LaskuRivi();     // Tyhjennetään uusi rivi
    }

    beginInsertRows( QModelIndex(), rivit_.count(), rivit_.count());
    rivit_.append(rivi);
    endInsertRows();
    emit summaMuuttunut(laskunSumma());
}

void LaskuModel::poistaRivi(int indeksi)
{
    beginRemoveRows(QModelIndex(), indeksi, indeksi);
    rivit_.removeAt(indeksi);
    endRemoveRows();
    emit summaMuuttunut(laskunSumma());
}

void LaskuModel::paivitaSumma(int rivi)
{
    emit dataChanged( createIndex(rivi, BRUTTOSUMMA, rivi), createIndex(rivi, BRUTTOSUMMA, rivi) );
    emit summaMuuttunut(laskunSumma());

}

LaskuRivi::LaskuRivi()
{
    if( kp()->asetukset()->onko("AlvVelvollinen"))
    {
        alvKoodi = AlvKoodi::MYYNNIT_NETTO;
        alvProsentti = VerotyyppiModel::oletusAlvProsentti();
    }
    else
        alvKoodi = AlvKoodi::EIALV;

    Tositelaji laji = kp()->tositelajit()->tositelaji( kp()->asetukset()->luku("LaskuTositelaji") );
    myyntiTili = kp()->tilit()->tiliNumerolla( laji.json()->luku("Oletustili") );
}

double LaskuRivi::yhteensaSnt() const
{
    // TODO: Eri alv-tyypeillä
    return std::round( maara *  ahintaSnt * (100 + alvProsentti ) / 100 );
}
