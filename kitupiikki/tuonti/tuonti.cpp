/*
   Copyright (C) 2018 Arto Hyvättinen

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

#include <QDebug>

#include <QSqlQuery>

#include "tuonti.h"
#include "pdftuonti.h"
#include "csvtuonti.h"

#include "kirjaus/kirjauswg.h"
#include "db/tili.h"
#include "db/eranvalintamodel.h"
#include "laskutus/laskumodel.h"

Tuonti::Tuonti(KirjausWg *wg)
    :  kirjausWg_(wg)
{

}

bool Tuonti::tuo(const QString &tiedostonnimi, KirjausWg *wg)
{

    if( tiedostonnimi.endsWith(".pdf", Qt::CaseInsensitive) )
    {
        PdfTuonti pdftuonti(wg);
        return pdftuonti.tuoTiedosto(tiedostonnimi);
    }
    else if( tiedostonnimi.endsWith(".csv", Qt::CaseInsensitive))
    {
        CsvTuonti csvtuonti(wg);
        return csvtuonti.tuoTiedosto(tiedostonnimi);
    }

    return true;
}

void Tuonti::tuoLasku(qlonglong sentit, QDate laskupvm, QDate toimituspvm, QDate erapvm, QString viite, QString tilinumero, QString saajanNimi)
{
    QDate pvm = toimituspvm;
    if( !toimituspvm.isValid() || kp()->asetukset()->luku("TuontiOstolaskuPeruste") == LASKUPERUSTEINEN)
        pvm = laskupvm;

    kirjausWg()->gui()->otsikkoEdit->setText(saajanNimi);
    kirjausWg()->gui()->tositePvmEdit->setDate(pvm);


    VientiRivi rivi;
    rivi.pvm = pvm;
    rivi.selite = saajanNimi;

    if( !tilinumero.isEmpty() &&  kp()->tilit()->tiliIbanilla(tilinumero).onkoValidi() )
    {
        // Oma tili eli onkin myyntilasku
        // Tositelajiksi myyntilaskut, mutta toistaiseksi ei muuten kirjaudu
        kirjausWg()->gui()->tositetyyppiCombo->setCurrentIndex(
                    kirjausWg()->gui()->tositetyyppiCombo->findData(TositelajiModel::MYYNTILASKUT, TositelajiModel::KirjausTyyppiRooli) );

        rivi.debetSnt = sentit;
    }
    else
    {
        kirjausWg()->gui()->tositetyyppiCombo->setCurrentIndex(
                    kirjausWg()->gui()->tositetyyppiCombo->findData( kp()->asetukset()->luku("TuontiOstolaskuTositelaji"), TositelajiModel::IdRooli ) );
        rivi.tili = kp()->tilit()->tiliNumerolla( kp()->asetukset()->luku("TuontiOstolaskuTili") );
        rivi.kreditSnt = sentit;
    }

    // Poistetaan viitenumeron etunollat
    viite.replace( QRegularExpression("^0*"),"");

    rivi.viite = viite;
    rivi.saajanTili = tilinumero;
    rivi.erapvm = erapvm;
    rivi.json.set("SaajanNimi", saajanNimi);

    kirjausWg()->model()->vientiModel()->lisaaVienti(rivi);
    kirjausWg()->tiedotModeliin();

}

bool Tuonti::tiliote(QString iban, QDate mista, QDate mihin)
{
    return tiliote( kp()->tilit()->tiliIbanilla(iban),
                    mista, mihin);
}

bool Tuonti::tiliote(Tili tili, QDate mista, QDate mihin)
{
    tiliotetili_ = tili;
    if( !tiliotetili_.onko(TiliLaji::PANKKITILI))
        return false;

    for(int i=0; i < kp()->tositelajit()->rowCount(QModelIndex()); i++)
    {
        QModelIndex index = kp()->tositelajit()->index(i,0);
        if( index.data(TositelajiModel::KirjausTyyppiRooli).toInt() == TositelajiModel::TILIOTE &&
            index.data(TositelajiModel::VastatiliNroRooli).toInt() == tiliotetili().numero())
        {
            // Tämä on kyseisen tiliotteen tositelaji
            kirjausWg()->gui()->tositetyyppiCombo->setCurrentIndex(
                        kirjausWg()->gui()->tositetyyppiCombo->findData( index.data(TositelajiModel::IdRooli), TositelajiModel::IdRooli ));
            break;
        }
    }


    kirjausWg()->gui()->tiliotetiliCombo->setCurrentIndex(
                kirjausWg()->gui()->tiliotetiliCombo->findData( tiliotetili().id(), TiliModel::IdRooli ));
    kirjausWg()->gui()->tilioteBox->setChecked(true);

    if( mista.isValid() && mihin.isValid())
    {
        kirjausWg()->gui()->tiliotealkaenEdit->setDate(mista);
        kirjausWg()->gui()->tilioteloppuenEdit->setDate(mihin);
        kirjausWg()->gui()->otsikkoEdit->setText( kp()->tr("Tiliote %1 - %2")
                                              .arg(mista.toString(Qt::SystemLocaleShortDate)).arg(mihin.toString(Qt::SystemLocaleShortDate)));
    }
    else
        kirjausWg()->gui()->otsikkoEdit->setText( kp()->tr("Tiliote"));

    kirjausWg()->gui()->tositePvmEdit->setDate(mihin);

    return true;
}

void Tuonti::oterivi(QDate pvm, qlonglong sentit, QString iban, QString viite, QString arkistotunnus, QString selite)
{
    // Etunollien poisto viiterivistä
    viite.replace( QRegularExpression("^0*"),"");


    // Tuplatuonnin esto
    QSqlQuery tupla( QString("SELECT id FROM vienti WHERE arkistotunnus='%1'").arg(arkistotunnus));
    if( tupla.next() )
        return;

    VientiRivi vastarivi;
    vastarivi.pvm = pvm;

    QStringList omaEhtoistenVerojenTilit;
    omaEhtoistenVerojenTilit << "FI6416603000117625" << "FI5689199710000724" << "FI3550000120253504";

    // Etsitään mahdolliset erät
    if( sentit > 0 && !viite.isEmpty())
    {
        QSqlQuery kysely( QString("SELECT avoinSnt, json, asiakas, kirjausperuste FROM lasku WHERE id=%1").arg(viite) );
        if( kysely.next())
        {
            if( kysely.value("avoinSnt").toLongLong() >= sentit )
            {
                // Kyseisellä viitteellä on avoin lasku, jota voidaan siis maksaa
                JsonKentta json( kysely.value("json").toByteArray() );
                selite = QString("%1 [%2]").arg( kysely.value("asiakas").toString(), viite );

                vastarivi.tili = kp()->tilit()->tiliNumerolla( json.luku("Saatavatili") );
                vastarivi.eraId = json.luku("TaseEra");
                vastarivi.maksaaLaskua = viite.toInt();
            }
            else if( kysely.value("kirjausperuste").toInt() == LaskuModel::MAKSUPERUSTE)
            {
                // Maksuperusteinen lasku, joka on jo kirjattu maksetuksi (avoin summa 0)
                // Tälle ei tehdä enää mitään muuta kirjausta eli ei edes lisätä tositteelle
                return;
            }
        }
    }
    else if(  sentit < 0 && omaEhtoistenVerojenTilit.contains(iban) )
    {
        // Verojen maksua, kohdistuu Verovelka-tilille
        vastarivi.tili = kp()->tilit()->tiliTyypilla(TiliLaji::VEROVELKA);

    }
    else if( sentit < 0 && !iban.isEmpty() && !viite.isEmpty())
    {
        // Ostolasku
        // Kirjataan vanhin lasku, joka täsmää senttimäärään ja joka vielä maksamatta

        QSqlQuery kysely( QString("SELECT id, tili, selite FROM vienti WHERE iban='%1' AND viite='%2' ORDER BY pvm")
                          .arg(iban).arg(viite) );
        qDebug() << kysely.lastQuery();
        while( kysely.next())
        {
            int eraId = kysely.value("id").toInt();
            TaseEra era( eraId );

            if( era.saldoSnt == sentit )
            {
                vastarivi.tili = kp()->tilit()->tiliIdlla( kysely.value("tili").toInt() );
                vastarivi.eraId = kysely.value("id").toInt();
                selite = kysely.value("selite").toString();

                break;
            }
        }
    }


    VientiRivi rivi;
    rivi.pvm = pvm;
    rivi.tili = tiliotetili();
    rivi.selite = selite;
    vastarivi.selite = selite;
    rivi.riviNro = kirjausWg()->model()->vientiModel()->seuraavaRiviNumero();

    if( sentit > 0)
    {
        rivi.debetSnt = sentit;
        vastarivi.kreditSnt = sentit;
    }
    else
    {
        rivi.kreditSnt = 0 - sentit;
        vastarivi.debetSnt = 0 - sentit;
    }

    rivi.arkistotunnus = arkistotunnus;
    kirjausWg()->model()->vientiModel()->lisaaVienti(rivi);
    kirjausWg()->model()->vientiModel()->lisaaVienti(vastarivi);

    qDebug() << pvm.toString(Qt::SystemLocaleShortDate) << sentit << iban << viite << arkistotunnus << selite;

}
