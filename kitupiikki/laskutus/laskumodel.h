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

#ifndef LASKUMODEL_H
#define LASKUMODEL_H

#include <QAbstractTableModel>
#include <QDate>
#include <QList>

#include "db/tili.h"
#include "db/kohdennus.h"
#include "db/tositelaji.h"

#include "laskutmodel.h"

/**
 * @brief Laskun yksi rivi
 * 
 * Käytetään myös tuoteluettelon tuotteesta
 */
struct LaskuRivi
{
    LaskuRivi();
    double yhteensaSnt() const;

    QString nimike;
    double maara = 1.00;
    QString yksikko;
    double ahintaSnt = 0.00;
    int alvKoodi;
    int alvProsentti = 0.00;
    Tili myyntiTili;
    Kohdennus kohdennus;
    int tuoteKoodi = 0;
};

/**
 * @brief Laskun tiedot
 */
class LaskuModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    LaskuModel(QObject *parent = 0, AvoinLasku hyvitettava = AvoinLasku());

    enum LaskuSarake
    {
        NIMIKE, MAARA, YKSIKKO, AHINTA, ALV, TILI, KOHDENNUS, BRUTTOSUMMA
    };

    enum Kirjausperuste {SUORITEPERUSTE, LASKUTUSPERUSTE, MAKSUPERUSTE, KATEISLASKU};


    enum
    {
        NimikeRooli = Qt::UserRole,
        AlvKoodiRooli = Qt::UserRole + 5,
        AlvProsenttiRooli = Qt::UserRole + 6,
        NettoRooli = Qt::UserRole + 7,
        VeroRooli = Qt::UserRole + 8,
        TuoteKoodiRooli = Qt::UserRole + 9
    };


    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QVariant data(const QModelIndex &index, int role) const;
    bool setData(const QModelIndex &index, const QVariant &value, int role);
    Qt::ItemFlags flags(const QModelIndex &index) const;

    int laskunSumma() const;

    /**
     * @brief Palauttaa kopion laskurivistä
     * @param indeksi Rivin indeksi (alkane nollasta)
     * @return 
     */
    LaskuRivi rivi(int indeksi) const;
    
    QDate pvm() const;
    QDate erapaiva() const { return erapaiva_; }
    QDate toimituspaiva() const { return toimituspaiva_; }
    QString lisatieto() const { return lisatieto_;}
    QString osoite() const { return osoite_; }
    QString laskunsaajanNimi() const { return laskunsaajanNimi_; }
    int kirjausperuste() const { return kirjausperuste_;}
    QString email() const { return email_;}
    AvoinLasku hyvityslasku() const { return hyvitettavaLasku_; }


    qulonglong laskunro() const;
    QString viitenumero() const;

    void asetaErapaiva(const QDate & paiva) { erapaiva_ = paiva; }
    void asetaLisatieto(const QString& tieto) { lisatieto_ = tieto; }
    void asetaOsoite(const QString& osoite) { osoite_ = osoite; }
    void asetaToimituspaiva(const QDate& pvm) { toimituspaiva_ = pvm; }
    void asetaLaskunsaajannimi(const QString& nimi) { laskunsaajanNimi_ = nimi; }
    void asetaKirjausperuste(int kirjausperuste) { kirjausperuste_ = kirjausperuste; }
    void asetaEmail(const QString& osoite) { email_ = osoite; }

    /**
     * @brief Tallentaa tämän laskun, jonka jälkeen model pitäisi unohtaa
     * @return
     */
    bool tallenna(Tili rahatili);

    /**
     * @brief Laskee viitenumeron tarkasteluvun
     * @param luvusta Viitenumero ilman tarkastetta
     * @return
     */
    static int laskeViiteTarkiste(qulonglong luvusta);

public slots:
    void lisaaRivi(LaskuRivi rivi = LaskuRivi());
    void poistaRivi(int indeksi);

signals:
    void summaMuuttunut(int summaSnt);

private:
    QList<LaskuRivi> rivit_;
    QDate erapaiva_;
    QDate toimituspaiva_;
    QString laskunsaajanNimi_;
    QString lisatieto_;
    QString osoite_;
    int kirjausperuste_;
    QString email_;
    AvoinLasku hyvitettavaLasku_;

    void paivitaSumma(int rivi);
};

#endif // LASKUMODEL_H
