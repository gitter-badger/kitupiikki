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

#ifndef LASKUNMAKSUDIALOGI_H
#define LASKUNMAKSUDIALOGI_H

#include <QDialog>
#include <QSortFilterProxyModel>

#include "laskutmodel.h"
#include "kirjaus/kirjauswg.h"


namespace Ui {
class LaskunMaksuDialogi;
}

/**
 * @brief Laskunmaksun kirjaamisen dialogi
 *
 * Näyttää listan avoimista laskuista, joista voipi sitten valita
 * haluamansa
 */
class LaskunMaksuDialogi : public QDialog
{
    Q_OBJECT

public:
    LaskunMaksuDialogi(KirjausWg *kirjauswg);
    ~LaskunMaksuDialogi();

private slots:
    void valintaMuuttuu();
    void kirjaa();
    void tarkistaKelpo();
    void naytaLasku();
    void suodata();

private:
    KirjausWg *kirjaaja;
    Ui::LaskunMaksuDialogi *ui;

    LaskutModel *laskut;
    QSortFilterProxyModel *proxy;
};

#endif // LASKUNMAKSUDIALOGI_H
