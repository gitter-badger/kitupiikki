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

#include "laskuntulostaja.h"
#include "db/kirjanpito.h"

#include <QDebug>
#include <cmath>

LaskunTulostaja::LaskunTulostaja(LaskuModel *model) : QObject(model), model_(model)
{
    // Hakee laskulle tulostuvan IBAN-numeron
    iban = kp()->tilit()->tiliNumerolla( kp()->asetukset()->luku("LaskuTili")).json()->str("IBAN");
}

bool LaskunTulostaja::tulosta(QPrinter *printer)
{

    QPainter painter(printer);
    double mm = printer->width() * 1.00 / printer->widthMM();
    qreal marginaali = 0.0;

    if( model_->laskunSumma() > 0 && model_->kirjausperuste() != LaskuModel::KATEISLASKU)
    {
        painter.translate( 0, painter.window().height() - mm * 95 );
        marginaali += alatunniste(printer, &painter) + mm * 95;
        tilisiirto(printer, &painter);
        painter.resetTransform();
    }
    else
    {
        painter.translate(0, painter.window().height());
        marginaali += alatunniste(printer, &painter);
    }
    painter.resetTransform();

    ylaruudukko(printer, &painter);
    lisatieto(&painter);
    erittely(printer, &painter, marginaali);
    painter.end();

    return true;
}

bool LaskunTulostaja::kirjoitaPdf(QString tiedostonnimi)
{
    QPrinter printer;
    printer.setPaperSize(QPrinter::A4);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName( tiedostonnimi );
    tulosta(&printer);
    return true;
}

QString LaskunTulostaja::html()
{
    QString txt = "<html><body><table width=100%>";


    QString osoite = model_->osoite();
    osoite.replace("\n","<br/>");

    QString omaosoite = kp()->asetukset()->asetus("Osoite");
    omaosoite.replace("\n","<br>");

    QString otsikko = tr("Lasku");
    if( model_->hyvityslasku().viitenro)
        otsikko = tr("Hyvityslasku laskulle %1").arg( model_->hyvityslasku().viitenro);
    else if(model_->kirjausperuste() == LaskuModel::KATEISLASKU)
        otsikko = tr("Kuitti");

    txt.append(tr("<tr><td rowspan=2 width=50%>%1<br>%2</td><td colspan=3>%3</td></tr>").arg(kp()->asetukset()->asetus("Nimi")).arg(omaosoite).arg(otsikko) );
    txt.append(tr("<tr><td width=25%>Laskun päivämäärä</td><td width=25%>%1</td></tr>").arg( kp()->paivamaara().toString(Qt::SystemLocaleShortDate) ));
    txt.append(tr("<tr><td rowspan=4>%1</td><td>Viitenumero</td><td>%2</td></td>").arg( osoite ).arg(model_->viitenumero() ));

    // Käteislaskulla tai hyvityslaskulla ei eräpäivää
    if( model_->kirjausperuste() != LaskuModel::KATEISLASKU && !model_->hyvityslasku().viitenro)
        txt.append(tr("<tr><td>Eräpäivä</td><td>%1</td></tr>").arg(model_->erapaiva().toString(Qt::SystemLocaleShortDate)));

    txt.append(tr("<tr><td>Summa</td><td>%L1 €</td>").arg( (model_->laskunSumma() / 100.0) ,0,'f',2));

    if( model_->kirjausperuste() != LaskuModel::KATEISLASKU && !model_->hyvityslasku().viitenro)
        txt.append(tr("<tr><td>IBAN</td><td>%1</td></tr>").arg( iban) );


    txt.append("</table><p>" + model_->lisatieto() + "</p><table width=100%>");

    bool alv = kp()->asetukset()->onko("AlvVelvollinen");

    if(alv)
        txt.append(tr("<tr><th>Nimike</th><th>Määrä</th><th>á netto</th><th>Alv %</th><th>Alv</th><th>Yhteensä</th></tr>"));
    else
        txt.append(tr("<tr><th>Nimike</th><th>Määrä</th><th>á netto</th><th>Yhteensä</th></tr>"));


    qreal kokoNetto = 0.0;
    qreal kokoVero = 0.0;

    QMap<int,AlvErittelyEra> alvit;

    for(int i=0; i < model_->rowCount(QModelIndex());i++)
    {
        int yhtsnt = model_->data( model_->index(i, LaskuModel::BRUTTOSUMMA), Qt::EditRole ).toInt();
        if( !yhtsnt)
            continue;   // Ohitetaan tyhjät rivit

        QString nimike = model_->data( model_->index(i, LaskuModel::NIMIKE), Qt::DisplayRole ).toString();
        QString maara = model_->data( model_->index(i, LaskuModel::MAARA), Qt::DisplayRole ).toString();
        QString yksikko = model_->data( model_->index(i, LaskuModel::YKSIKKO), Qt::DisplayRole ).toString();
        QString ahinta = model_->data( model_->index(i, LaskuModel::AHINTA), Qt::DisplayRole ).toString();
        QString vero = model_->data( model_->index(i, LaskuModel::ALV), Qt::DisplayRole ).toString();
        QString yht = model_->data( model_->index(i, LaskuModel::BRUTTOSUMMA), Qt::DisplayRole ).toString();

        double verosnt = model_->data( model_->index(i, LaskuModel::NIMIKE), LaskuModel::VeroRooli ).toDouble();
        double nettosnt = model_->data( model_->index(i, LaskuModel::NIMIKE), LaskuModel::NettoRooli ).toDouble();
        int veroprossa = model_->data( model_->index(i, LaskuModel::NIMIKE), LaskuModel::AlvProsenttiRooli ).toInt();

        kokoNetto += nettosnt;

        if( alv )
        {
            txt.append(QString("<tr><td>%1</td><td>%2 %3</td><td>%4 €</td><td>%5</td><td>%L6 €</td><td>%7</td><tr>")
                       .arg(nimike).arg(maara).arg(yksikko).arg(ahinta).arg(vero).arg(verosnt / 100.0,0,'f',2).arg(yht) );

            // Lisätään alv-erittelyä varten summataulukkoihin
            if( !alvit.contains(veroprossa))
                alvit.insert(veroprossa, AlvErittelyEra() );

            alvit[veroprossa].netto += nettosnt;
            alvit[veroprossa].vero += verosnt;
            kokoVero += verosnt;
        }
        else
        {
            txt.append(QString("<tr><td>%1</td><td>%2 %3</td><td>%4 €</td><td>%L5 €</td><tr>")
                       .arg(nimike).arg(maara).arg(yksikko).arg(ahinta).arg(yht) );            
        }
    }
    txt.append("</table><p>");
    if( alv)
    {
        txt.append("<table width=50%><tr><th>Alv%</th><th>Veroton</th><th>Vero</th><th>Yhtensä</th></tr>");
        QMapIterator<int,AlvErittelyEra> iter(alvit);
        while(iter.hasNext())
        {
            iter.next();
            txt.append( tr("<tr><td>%1 %</td><td>%L2 €</td><td>%L3 €</td><td>%L4 €</td><tr>")
                        .arg( iter.key())
                        .arg( ( iter.value().netto / 100.0) ,0,'f',2)
                        .arg( ( iter.value().vero / 100.0) ,0,'f',2)
                        .arg( (iter.value().brutto() / 100.0) ,0,'f',2) ) ;
        }
        txt.append( tr("<tr><td>YHTEENSÄ </td><td>%L1 €</td><td>%L2 €</td><td>%L3 €</td><tr>")
                    .arg( ( kokoNetto / 100.0) ,0,'f',2)
                    .arg( ( kokoVero / 100.0) ,0,'f',2)
                    .arg( ( model_->laskunSumma() / 100.0) ,0,'f',2) ) ;
        txt.append("</table>");

    }

    txt.append( tr("<hr>Y-tunnus %1<br>Puhelin %2").arg(kp()->asetukset()->asetus("Ytunnus")).arg(kp()->asetukset()->asetus("Puhelin")) );

    txt.append("</body></html>");

    return txt;
}

void LaskunTulostaja::ylaruudukko(QPrinter *printer, QPainter *painter)
{
    const int TEKSTIPT = 10;
    const int OTSAKEPT = 7;

    double mm = printer->width() * 1.00 / printer->widthMM();
    double rk = QFontMetrics( QFont("Sans",OTSAKEPT)).height() + QFontMetrics(QFont("Sans",TEKSTIPT)).height() + 2 * mm;  // rivinkorkeus

    double vasen = 0.0;
    double leveys = painter->window().width();

    if( QFile::exists(kp()->hakemisto().absoluteFilePath("logo128.png")))
    {
        painter->drawPixmap( QRectF(0, 0, rk * 2, rk * 2), QPixmap( kp()->hakemisto().absoluteFilePath("logo128.png") ),
                             QRect(0,0,128,128));
        vasen += rk * 2.2;
    }
    painter->setFont(QFont("Sans",14));
    double pv = painter->fontMetrics().height();
    painter->drawText(QRectF(vasen, 0, leveys / 2 - vasen, pv ), Qt::AlignLeft, kp()->asetus("Nimi"));

    painter->setFont(QFont("Sans",8));
    QRectF osoiteRect = painter->boundingRect( QRectF(vasen,pv,leveys/2,rk * 10), Qt::TextWordWrap, kp()->asetus("Osoite") );
    painter->drawText(osoiteRect, Qt::AlignLeft, kp()->asetus("Osoite") );

    pv += osoiteRect.height() ;     // pv = perusviiva
    if( pv < rk * 2.2)              // Jotta logo mahtuu
        pv = rk * 2.2;


    painter->setPen( QPen( QBrush(Qt::black), 0.13));
    painter->drawLine( QLineF(0, pv, leveys, pv ));
    painter->drawLine( QLineF(leveys/2, pv-rk, leveys, pv-rk ));
    painter->drawLine( QLineF(leveys/2, pv-rk, leveys/2, pv+rk*4));

    painter->drawLine( QLineF(0, pv+ rk*4, leveys, pv + rk*4));   
    painter->drawLine(QLineF(3*leveys/4, pv+rk*2, 3*leveys/4, pv+rk*4));
    for(int i=1; i<4; i++)
        painter->drawLine(QLineF(leveys/2, pv + i * rk, leveys, pv + i * rk));

    painter->setFont( QFont("Sans",OTSAKEPT) );
    if( model_->kirjausperuste() != LaskuModel::MAKSUPERUSTE)
    {
        painter->drawLine(QLineF(3*leveys/4, pv-rk, 3*leveys/4, pv));
        painter->drawText(QRectF( 3 * leveys / 4 + mm, pv - rk + mm, leveys / 4, rk ), Qt::AlignTop, tr("Kirjanpidon tositenro"));
    }

    painter->drawText(QRectF( leveys / 2 + mm, pv - rk + mm, leveys / 4, rk ), Qt::AlignTop, tr("Päivämäärä"));
    painter->drawText(QRectF( leveys / 2 + mm, pv + mm, leveys / 4, rk ), Qt::AlignTop, tr("Toimituspäivä"));
    painter->drawText(QRectF( leveys / 2 + mm, pv + rk + mm, leveys / 4, rk ), Qt::AlignTop, tr("Viitenumero"));
    painter->drawText(QRectF( leveys / 2 + mm, pv + rk * 2 + mm, leveys / 4, rk ), Qt::AlignTop, tr("Eräpäivä"));
    painter->drawText(QRectF( 3 * leveys / 4 + mm, pv + rk * 2 + mm, leveys / 4, rk ), Qt::AlignTop, tr("Summa"));
    painter->drawText(QRectF( leveys / 2 + mm, pv + rk * 3 + mm, leveys / 4, rk ), Qt::AlignTop, tr("Huomautusaika"));
    painter->drawText(QRectF( 3 * leveys / 4 + mm, pv + rk * 3 + mm, leveys / 4, rk ), Qt::AlignTop, tr("Viivästyskorko"));


    painter->setFont(QFont("Sans", TEKSTIPT));

    painter->drawText(QRectF( mm*2, pv + mm * 2, leveys / 2 - mm * 4, rk * 4 - mm * 2), Qt::TextWordWrap, model_->osoite());

    // Haetaan tositetunniste
    if( model_->kirjausperuste() != LaskuModel::MAKSUPERUSTE)
    {
        Tositelaji laji = kp()->tositelajit()->tositelaji( kp()->asetukset()->luku("LaskuTositelaji") );
        painter->drawText(QRectF( 3 * leveys / 4 + mm, pv - rk, leveys / 4, rk-mm ), Qt::AlignBottom, QString("%1%2/%3")
                          .arg(laji.tunnus()).arg(laji.seuraavanTunnistenumero( model_->pvm() ))
                          .arg( kp()->tilikausiPaivalle(kp()->paivamaara()).kausitunnus())  );
    }

    painter->drawText(QRectF( leveys / 2 + mm, pv - rk, leveys / 4, rk-mm ), Qt::AlignBottom, kp()->paivamaara().toString(Qt::SystemLocaleShortDate) );
    painter->drawText(QRectF( leveys / 2 + mm, pv + mm, leveys / 4, rk-mm ), Qt::AlignBottom,  model_->toimituspaiva().toString(Qt::SystemLocaleShortDate) );
    painter->drawText(QRectF( leveys / 2 + mm, pv+rk+mm, leveys / 2, rk-mm ), Qt::AlignBottom, model_->viitenumero() );



    if( model_->kirjausperuste() == LaskuModel::KATEISLASKU)
    {
        painter->drawText(QRectF( leveys / 2 + mm, pv - rk * 2, leveys / 4, rk-mm ), Qt::AlignBottom,  tr("Käteislasku / Kuitti") );
        painter->drawText(QRectF( leveys / 2 + mm, pv + rk * 2, leveys / 4, rk-mm ), Qt::AlignBottom,  tr("Maksettu") );
    }
    else if( model_->hyvityslasku().viitenro)
    {
        painter->drawText(QRectF( leveys / 2 + mm, pv - rk * 2, leveys / 4, rk-mm ), Qt::AlignBottom,  tr("Hyvityslasku laskulle %1")
                          .arg( model_->hyvityslasku().viitenro ));
    }
    else
    {
        painter->drawText(QRectF( leveys / 2 + mm, pv - rk * 2, leveys / 4, rk-mm ), Qt::AlignBottom,  tr("Lasku") );
        if( model_->laskunSumma() > 0.0 )  // Näytetään eräpäivä vain jos on maksettavaa
        {
            painter->drawText(QRectF( leveys / 2 + mm, pv + rk * 2, leveys / 4, rk-mm ), Qt::AlignBottom,  model_->erapaiva().toString(Qt::SystemLocaleShortDate) );
            painter->drawText(QRectF( 3 * leveys / 4 + mm, pv + rk * 3, leveys / 4, rk-mm ), Qt::AlignBottom,  kp()->asetus("LaskuViivastyskorko") );
        }

    }

    painter->drawText(QRectF( 3 * leveys / 4 + mm, pv + rk * 2, leveys / 4, rk-mm ), Qt::AlignBottom,  QString("%L1 €").arg( (model_->laskunSumma() / 100.0) ,0,'f',2));
    painter->drawText(QRectF( leveys / 2 + mm, pv + rk * 3, leveys / 4, rk-mm ), Qt::AlignBottom,  kp()->asetus("LaskuHuomautusaika") );


    painter->translate(0, pv + 4*rk + 5 * mm);
}

void LaskunTulostaja::lisatieto(QPainter *painter)
{
    painter->setFont( QFont("Sans", 10));
    QRectF ltRect = painter->boundingRect(QRect(0,0,painter->window().width(), painter->window().height()), Qt::TextWordWrap, model_->lisatieto());
    painter->drawText(ltRect, Qt::TextWordWrap, model_->lisatieto());
    if( ltRect.height() )
        painter->translate( 0, ltRect.height() + painter->fontMetrics().height());  // Vähän väliä


}

qreal LaskunTulostaja::alatunniste(QPrinter *printer,QPainter *painter)
{
    painter->setFont( QFont("Sans",10));
    qreal rk = painter->fontMetrics().height();
    painter->save();
    painter->translate(0, -1.5 * rk);

    qreal leveys = painter->window().width();
    double mm = printer->width() * 1.00 / printer->widthMM();

    painter->drawText(QRectF(0,0,leveys/3,rk), Qt::AlignLeft, tr("Puh. %1").arg(kp()->asetus("Puhelin")));
    painter->drawText(QRectF(leveys / 3,0,leveys/3,rk), Qt::AlignCenter, tr("IBAN %1").arg( iban ));
    painter->drawText(QRectF(2 *leveys / 3,0,leveys/3,rk), Qt::AlignRight, tr("Y-tunnus %1").arg(kp()->asetus("Ytunnus")));
    painter->setPen( QPen(QBrush(Qt::black), mm * 0.13));
    painter->drawLine( QLineF( 0, 0, leveys, 0));

    bool rakennuspalvelut = false;
    bool yhteisomyynti = false;

    // Tarkistetaan verotyypit ja niiden mukaiset tekstit
    for(int i=0; i<model_->rowCount(QModelIndex()); i++)
    {
        int verokoodi = model_->data( model_->index(i,0), LaskuModel::AlvKoodiRooli ).toInt();
        if( verokoodi == AlvKoodi::RAKENNUSPALVELU_MYYNTI)
            rakennuspalvelut = true;
        else if( verokoodi == AlvKoodi::YHTEISOHANKINNAT_TAVARAT || verokoodi == AlvKoodi::YHTEISOMYYNTI_PALVELUT)
            yhteisomyynti = true;
    }
    painter->translate(0, -2 * rk);

    if( yhteisomyynti)
    {
        painter->drawText(QRectF(0,0,leveys,rk), tr("AVL 72 a §: ALV 0% Yhteisömyynti  / VAT 0% Intra Community supply"));
        painter->translate(0, -1 * rk);
    }
    if( rakennuspalvelut)
        painter->drawText(QRectF(0,0,leveys,rk), tr("AVL 8 c §: Käännetty verovelvollisuus / Reverse charge"));


    qreal tila = 0 - painter->transform().dy();
    painter->restore();
    return tila;
}

void LaskunTulostaja::erittely(QPrinter *printer, QPainter *painter, qreal marginaali)
{
    bool alv = kp()->asetukset()->onko("AlvVelvollinen");
    erittelyOtsikko(printer, painter, alv);
    double mm = printer->width() * 1.00 / printer->widthMM();

    painter->setFont( QFont("Sans",10));
    qreal leveys = painter->window().width();
    qreal korkeus = painter->window().height() - marginaali;
    qreal rk = painter->fontMetrics().height();
    qreal kokoNetto = 0.0;
    qreal kokoVero = 0.0;

    QMap<int,AlvErittelyEra> alvit;

    for(int i=0; i < model_->rowCount(QModelIndex());i++)
    {
        int yhtsnt = model_->data( model_->index(i, LaskuModel::BRUTTOSUMMA), Qt::EditRole ).toInt();
        if( !yhtsnt)
            continue;   // Ohitetaan tyhjät rivit

        QString nimike = model_->data( model_->index(i, LaskuModel::NIMIKE), Qt::DisplayRole ).toString();
        QString maara = model_->data( model_->index(i, LaskuModel::MAARA), Qt::DisplayRole ).toString();
        QString yksikko = model_->data( model_->index(i, LaskuModel::YKSIKKO), Qt::DisplayRole ).toString();
        QString ahinta = model_->data( model_->index(i, LaskuModel::AHINTA), Qt::DisplayRole ).toString();
        QString vero = model_->data( model_->index(i, LaskuModel::ALV), Qt::DisplayRole ).toString();
        QString yht = model_->data( model_->index(i, LaskuModel::BRUTTOSUMMA), Qt::DisplayRole ).toString();

        double verosnt = model_->data( model_->index(i, LaskuModel::NIMIKE), LaskuModel::VeroRooli ).toDouble();
        double nettosnt = model_->data( model_->index(i, LaskuModel::NIMIKE), LaskuModel::NettoRooli ).toDouble();
        int veroprossa = model_->data( model_->index(i, LaskuModel::NIMIKE), LaskuModel::AlvProsenttiRooli ).toInt();

        // Lisätään alv-erittelyä varten summataulukkoihin
        if( !alvit.contains(veroprossa))
            alvit.insert(veroprossa, AlvErittelyEra() );

        alvit[veroprossa].netto += nettosnt;
        alvit[veroprossa].vero += verosnt;
        kokoNetto += nettosnt;
        kokoVero += verosnt;


        QRectF rect = alv ? painter->boundingRect(QRectF(0,0,leveys/2, leveys), Qt::TextWordWrap, nimike ) : painter->boundingRect(QRectF(0,0,3*leveys/8, leveys), Qt::TextWordWrap, nimike );

        qreal tamarivi = rect.height() ? rect.height() : rk;
        if( tamarivi + painter->transform().dy() > korkeus - 4 * rk )
        {
            painter->drawText(QRectF(7*leveys/8,0,leveys/8,rk), Qt::AlignLeft, tr("Jatkuu ..."));
            printer->newPage();
            painter->resetTransform();
            korkeus = painter->window().height();
            erittelyOtsikko(printer, painter, alv);
        }
        painter->drawText(rect, Qt::TextWordWrap, nimike);

        if( alv )
        {
            painter->drawText(QRectF(5 *leveys / 16,0,leveys/16-mm,rk), Qt::AlignRight, maara);
            painter->drawText(QRectF(6 *leveys / 16 + mm ,0,leveys/16,rk), Qt::AlignLeft, yksikko);

            painter->drawText(QRectF(7 *leveys / 16,0,leveys/8,rk), Qt::AlignRight, ahinta);
            painter->drawText(QRectF(9 *leveys / 16,0,leveys/8,rk), Qt::AlignRight, QString("%L1 €").arg( (nettosnt / 100.0) ,0,'f',2)  );

            if( vero.endsWith("%"))
            {
                painter->drawText(QRectF(11 *leveys / 16,0,leveys/16,rk), Qt::AlignRight, vero);
                painter->drawText(QRectF(12 *leveys / 16,0,leveys/8,rk), Qt::AlignRight, QString("%L1 €").arg( (verosnt / 100.0) ,0,'f',2) );
            }
            else
                painter->drawText(QRectF(11 *leveys / 16,0,3* leveys/16,rk), Qt::AlignCenter, vero);

            painter->drawText(QRectF(7*leveys/8,0,leveys/8,rk), Qt::AlignRight, yht);
        }
        else
        {
            painter->drawText(QRectF(10 *leveys / 16,0,leveys/16-mm,rk), Qt::AlignRight, maara);
            painter->drawText(QRectF(11 *leveys / 16 + mm ,0,leveys/16,rk), Qt::AlignLeft, yksikko);
            painter->drawText(QRectF(6 *leveys / 8,0,leveys/8,rk), Qt::AlignRight, ahinta);
            painter->drawText(QRectF(7*leveys/8,0,leveys/8,rk), Qt::AlignRight, yht);
        }

        painter->translate(0, tamarivi);
    }

    // ALV-erittelyn tulostus
    if( alv )
    {
        painter->translate( 0, rk * 0.5);
        painter->drawLine(QLineF(7*leveys / 16.0, 0, leveys, 0));
        painter->drawText(QRectF(7 *leveys / 16,0,leveys/8,rk), Qt::AlignLeft, tr("Alv-erittely")  );
        QMapIterator<int,AlvErittelyEra> iter(alvit);
        while(iter.hasNext())
        {
            iter.next();
            painter->drawText(QRectF(9 *leveys / 16,0,leveys/8,rk), Qt::AlignRight, QString("%L1 €").arg( ( iter.value().netto / 100.0) ,0,'f',2)  );
            painter->drawText(QRectF(11 *leveys / 16,0,leveys/16,rk), Qt::AlignRight, QString("%1 %").arg( iter.key() ) );
            painter->drawText(QRectF(12 *leveys / 16,0,leveys/8,rk), Qt::AlignRight, QString("%L1 €").arg( ( iter.value().vero / 100.0) ,0,'f',2) );
            painter->drawText(QRectF(7*leveys/8,0,leveys/8,rk), Qt::AlignRight, QString("%L1 €").arg( ( iter.value().brutto() / 100.0) ,0,'f',2) );
            painter->translate(0, rk);
        }
    }
    painter->translate(0, rk * 0.25);

    qreal yhtviivaAlkaa = alv == true ? 7 * leveys / 16.0 : 10 * leveys / 16.0; // ilman alviä lyhyempi yhteensä-viiva

    painter->drawLine(QLineF(yhtviivaAlkaa, -0.26 * mm , leveys, -0.26 * mm));
    painter->drawLine(QLineF(yhtviivaAlkaa, 0, leveys, 0));
    painter->drawText(QRectF(yhtviivaAlkaa, 0,leveys/8,rk), Qt::AlignLeft, tr("Yhteensä")  );

    if( alv )
    {
        painter->drawText(QRectF(9 *leveys / 16,0,leveys/8,rk), Qt::AlignRight, QString("%L1 €").arg( ( kokoNetto / 100.0) ,0,'f',2)  );
        painter->drawText(QRectF(12 *leveys / 16,0,leveys/8,rk), Qt::AlignRight, QString("%L1 €").arg( ( kokoVero / 100.0) ,0,'f',2) );
    }
    painter->drawText(QRectF(7*leveys/8,0,leveys/8,rk), Qt::AlignRight, QString("%L1 €").arg( ( model_->laskunSumma() / 100.0) ,0,'f',2) );


}

void LaskunTulostaja::erittelyOtsikko(QPrinter *printer, QPainter *painter, bool alv)
{
    painter->setFont( QFont("Sans",8));
    qreal rk = painter->fontMetrics().height();
    qreal leveys = painter->window().width();
    double mm = printer->width() * 1.00 / printer->widthMM();

    painter->drawText(QRectF(0,0,leveys/2,rk), Qt::AlignLeft, tr("Nimike"));

    if( alv )
    {
        painter->drawText(QRectF(5 *leveys / 16,0,leveys/8,rk), Qt::AlignHCenter, tr("kpl"));
        painter->drawText(QRectF(7 *leveys / 16,0,leveys/8,rk), Qt::AlignHCenter, tr("á netto"));
        painter->drawText(QRectF(9 *leveys / 16,0,leveys/8,rk), Qt::AlignHCenter, tr("netto"));
        painter->drawText(QRectF(11 *leveys / 16,0,leveys/16,rk), Qt::AlignHCenter, tr("alv"));
        painter->drawText(QRectF(12 *leveys / 16,0,leveys/8,rk), Qt::AlignHCenter, tr("vero"));
    }
    else
    {
        painter->drawText(QRectF(5 *leveys / 8,0,leveys/8,rk), Qt::AlignHCenter, tr("kpl"));
        painter->drawText(QRectF(6 *leveys / 8,0,leveys/8,rk), Qt::AlignHCenter, tr("á hinta"));
    }
    painter->drawText(QRectF(7 *leveys / 8,0,leveys/8,rk), Qt::AlignHCenter, tr("yhteensä"));
    painter->setPen( QPen(QBrush(Qt::black), mm * 0.13));
    painter->drawLine( QLineF( 0, rk, leveys, rk));
    painter->translate(0, rk * 1.1);

}

void LaskunTulostaja::tilisiirto(QPrinter *printer, QPainter *painter)
{
    painter->setFont(QFont("Sans", 7));
    double mm = printer->width() * 1.00 / printer->widthMM();

    painter->drawText( QRectF(0,0,mm*19,mm*16.9), Qt::AlignRight | Qt::AlignHCenter, tr("Saajan\n tilinumero\n Mottagarens\n kontonummer"));
    painter->drawText( QRectF(0, mm*18, mm*19, mm*14.8), Qt::AlignRight | Qt::AlignHCenter, tr("Saaja\n Mottagare"));
    painter->drawText( QRectF(0, mm*32.7, mm*19, mm*20), Qt::AlignRight | Qt::AlignTop, tr("Maksajan\n nimi ja\n osoite\n Betalarens\n namn och\n address"));
    painter->drawText( QRectF(0, mm*51.3, mm*19, mm*10), Qt::AlignRight | Qt::AlignBottom , tr("Allekirjoitus\n Underskrift"));
    painter->drawText( QRectF(0, mm*62.3, mm*19, mm*8.5), Qt::AlignRight | Qt::AlignHCenter, tr("Tililtä nro\n Från konto nr"));
    painter->drawText( QRectF(mm * 22, 0, mm*20, mm*10), Qt::AlignLeft, tr("IBAN"));

    painter->drawText( QRectF(mm*112.4, mm*53.8, mm*15, mm*8.5), Qt::AlignLeft | Qt::AlignTop, tr("Viitenumero\nRef.nr."));
    painter->drawText( QRectF(mm*112.4, mm*62.3, mm*15, mm*8.5), Qt::AlignLeft | Qt::AlignTop, tr("Eräpäivä\nFörfallodag"));
    painter->drawText( QRectF(mm*159, mm*62.3, mm*19, mm*8.5), Qt::AlignLeft, tr("Euro"));


    painter->setFont(QFont("Sans",6));
    painter->drawText( QRectF( mm * 140, mm * 72, mm * 60, mm * 20), Qt::AlignLeft | Qt::TextWordWrap, tr("Maksu välitetään saajalle maksujenvälityksen ehtojen "
                                                                                                          "mukaisesti ja vain maksajan ilmoittaman tilinumeron perusteella.\n"
                                                                                                          "Betalning förmedlas till mottagaren enligt villkoren för "
                                                                                                          "betalningsförmedling och endast till det kontonummer som "
                                                                                                          "betalaren angivit.") );
    painter->setPen( QPen( QBrush(Qt::black), mm * 0.5));
    painter->drawLine(QLineF(mm*111.4,0,mm*111.4,mm*69.8));
    painter->drawLine(QLineF(0, mm*16.9, mm*111.4, mm*16.9));
    painter->drawLine(QLineF(0, mm*31.7, mm*111.4, mm*31.7));
    painter->drawLine(QLineF(mm*20, 0, mm*20, mm*31.7));
    painter->drawLine(QLineF(0, mm*61.3, mm*205, mm*61.3));
    painter->drawLine(QLineF(0, mm*69.8, mm*205, mm*69.8));
    painter->drawLine(QLineF(mm*111.4, mm*52.8, mm*205, mm*52.8));
    painter->drawLine(QLineF(mm*131.4, mm*52.8, mm*131.4, mm*69.8));
    painter->drawLine(QLineF(mm*158, mm*61.3, mm*158, mm*69.8));
    painter->drawLine(QLineF(mm*20, mm*61.3, mm*20, mm*69.8));

    painter->setPen( QPen(QBrush(Qt::black), mm * 0.13));
    painter->drawLine( QLineF( mm*22, mm*57.1, mm*108, mm*57.1));

    painter->setPen( QPen(QBrush(Qt::black), mm * 0.13, Qt::DashLine));
    painter->drawLine( QLineF( 0, -1 * mm, painter->window().width(), -1 * mm));

    painter->setFont(QFont("Sans", 10));

    painter->drawText(QRectF( mm*22, mm * 33, mm * 90, mm * 25), Qt::TextWordWrap, model_->osoite());

    painter->drawText( QRectF(mm*133.4, mm*53.8, mm*60, mm*7.5), Qt::AlignLeft | Qt::AlignBottom, model_->viitenumero() );

    painter->drawText( QRectF(mm*133.4, mm*62.3, mm*30, mm*7.5), Qt::AlignLeft | Qt::AlignBottom, model_->erapaiva().toString(Qt::SystemLocaleShortDate) );
    painter->drawText( QRectF(mm*165, mm*62.3, mm*30, mm*7.5), Qt::AlignRight | Qt::AlignBottom, QString("%L1").arg( (model_->laskunSumma() / 100.0) ,0,'f',2) );

    painter->drawText( QRectF(mm*22, mm*17, mm*90, mm*13), Qt::AlignTop | Qt::TextWordWrap, kp()->asetus("Nimi") + "\n" + kp()->asetus("Osoite")  );
    painter->drawText( QRectF(mm*22, 0, mm*90, mm*17), Qt::AlignVCenter , iban  );


    painter->save();
    painter->setFont(QFont("Sans", 7.5));
    painter->translate(mm * 2, mm* 60);
    painter->rotate(-90.0);
    painter->drawText(0,0,tr("TILISIIRTO. GIRERING"));
    painter->restore();
}

