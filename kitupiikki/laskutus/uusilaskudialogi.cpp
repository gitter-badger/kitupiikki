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

#include "db/kirjanpito.h"

#include "uusilaskudialogi.h"
#include "ui_uusilaskudialogi.h"

UusiLaskuDialogi::UusiLaskuDialogi(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UusiLaskuDialogi)
{
    ui->setupUi(this);

    ui->perusteCombo->addItem("Suoriteperusteinen", SUORITEPERUSTE);
    ui->perusteCombo->addItem("Laskutusperusteinen", LASKUTUSPERUSTE);
    ui->perusteCombo->addItem("Maksuperusteinen", MAKSUPERUSTE);
    ui->perusteCombo->addItem("Käteiskuitti", KATEISLASKU);

    ui->toimitusDate->setDate( kp()->paivamaara() );
    ui->eraDate->setDate( kp()->paivamaara().addDays(14));
}

UusiLaskuDialogi::~UusiLaskuDialogi()
{
    delete ui;
}
