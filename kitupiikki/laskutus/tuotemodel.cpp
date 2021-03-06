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

#include "tuotemodel.h"
#include "db/kirjanpito.h"

#include <QSqlQuery>
#include <QDebug>
#include <QSqlError>

TuoteModel::TuoteModel(QObject *parent) :
    QAbstractTableModel(parent)
{
    
}

int TuoteModel::rowCount(const QModelIndex & /* parent */) const
{
    return tuotteet_.count();    
}

int TuoteModel::columnCount(const QModelIndex & /* parent */) const
{
    return 2;
}

QVariant TuoteModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if( role == Qt::TextAlignmentRole)
        return QVariant( Qt::AlignCenter);
    else if( role == Qt::DisplayRole && orientation == Qt::Horizontal)
    {
        if( section == NIMIKE )
            return tr("Nimike");
        else if(section == HINTA)
            return tr("Hinta");
    }
    return QVariant();
}

QVariant TuoteModel::data(const QModelIndex &index, int role) const
{
    if( !index.isValid())
        return QVariant();
    
    LaskuRivi rivi = tuotteet_.value(index.row());
    
    if( role == Qt::DisplayRole)
    {
        if( index.column() == NIMIKE )
            return rivi.nimike;
        else if(index.column() == HINTA)
        {
            double bruttohinta = (100.0 + rivi.alvProsentti) * rivi.ahintaSnt / 100.0;
            return QString("%L1 €").arg(bruttohinta / 100.0,0,'f',2);
        }
    }
    return QVariant();
}

int TuoteModel::lisaaTuote(LaskuRivi tuote)
{

    QSqlQuery kysely;
    kysely.prepare("INSERT INTO tuote(nimike, yksikko, hintaSnt, alvkoodi, alvprosentti, tili, kohdennus) "
                   "VALUES(:nimike, :yksikko, :hinta, :alvkoodi, :alvprosentti, :tili, :kohdennus) ");
    kysely.bindValue(":nimike", tuote.nimike);
    kysely.bindValue(":yksikko", tuote.yksikko);
    kysely.bindValue(":hinta", tuote.ahintaSnt);
    kysely.bindValue(":alvkoodi", tuote.alvKoodi);
    kysely.bindValue(":alvprosentti", tuote.alvProsentti);
    kysely.bindValue(":tili", tuote.myyntiTili.id());
    kysely.bindValue(":kohdennus", tuote.kohdennus.id());
    kysely.exec();

    tuote.tuoteKoodi = kysely.lastInsertId().toInt();
    tuote.maara = 1.00;     // Tuoteluettelossa ei määrätietoa


    beginInsertRows(QModelIndex(), tuotteet_.count(), tuotteet_.count() );
    tuotteet_.append( tuote );
    endInsertRows();

    qDebug() << kysely.lastQuery();
    qDebug() << kysely.lastError().text();

    return tuote.tuoteKoodi;
}

void TuoteModel::poistaTuote(int indeksi)
{
    QSqlQuery kysely( QString("delete from tuote where id=%1 ").arg(tuotteet_.value(indeksi).tuoteKoodi) );
    kysely.exec();

    beginRemoveRows(QModelIndex(), indeksi, indeksi);
    tuotteet_.removeAt(indeksi);
    endRemoveRows();
}

void TuoteModel::paivitaTuote(LaskuRivi tuote)
{
    int indeksi = 0;
    // Etsitään tuote koodilla
    for(; indeksi < tuotteet_.count(); indeksi++)
        if( tuotteet_.value(indeksi).tuoteKoodi == tuote.tuoteKoodi)
            break;

    tuote.maara = 1.00;
    tuotteet_[indeksi] = tuote;

    QSqlQuery kysely;
    kysely.prepare("UPDATE tuote SET nimike=:nimike, yksikko=:yksikko, hintaSnt=:hinta, "
                   "alvkoodi=:alvkoodi, alvprosentti=:alvprosentti, tili=:tili, kohdennus=:kohdennus "
                   "WHERE id=:id");
    kysely.bindValue(":id", tuote.tuoteKoodi);
    kysely.bindValue(":nimike", tuote.nimike);
    kysely.bindValue(":yksikko", tuote.yksikko);
    kysely.bindValue(":hinta", tuote.ahintaSnt);
    kysely.bindValue(":alvkoodi", tuote.alvKoodi);
    kysely.bindValue(":alvprosentti", tuote.alvProsentti);
    kysely.bindValue(":tili", tuote.myyntiTili.id());
    kysely.bindValue(":kohdennus", tuote.kohdennus.id());
    kysely.exec();

    emit dataChanged(index(indeksi, 0), index(indeksi,1));
}

LaskuRivi TuoteModel::tuote(int indeksi) const
{
    return tuotteet_.value(indeksi);
}

void TuoteModel::lataa()
{
    beginResetModel();
    tuotteet_.clear();
    QSqlQuery kysely;
    kysely.exec("SELECT id,nimike,yksikko,hintaSnt,alvkoodi,alvprosentti,tili,kohdennus FROM tuote ORDER BY nimike");
    while(kysely.next())
    {
        LaskuRivi tuote;
        tuote.tuoteKoodi = kysely.value("id").toInt();
        tuote.nimike = kysely.value("nimike").toString();
        tuote.yksikko = kysely.value("yksikko").toString();
        tuote.ahintaSnt = kysely.value("hintaSnt").toDouble();
        tuote.alvKoodi = kysely.value("alvkoodi").toInt();
        tuote.alvProsentti = kysely.value("alvprosentti").toInt();
        tuote.myyntiTili = kp()->tilit()->tiliIdlla( kysely.value("tili").toInt() );
        tuote.kohdennus = kp()->kohdennukset()->kohdennus( kysely.value("kohdennus").toInt() );
        tuotteet_.append(tuote);
    }
    endResetModel();

}
