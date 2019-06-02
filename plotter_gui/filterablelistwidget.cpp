#include "filterablelistwidget.h"
#include "ui_filterablelistwidget.h"
#include "PlotJuggler/alphanum.hpp"
#include <QDebug>
#include <QLayoutItem>
#include <QMenu>
#include <QSettings>
#include <QDrag>
#include <QMimeData>
#include <QHeaderView>
#include <QFontDatabase>
#include <QMessageBox>
#include <QApplication>
#include <QPainter>
#include <QCompleter>
#include <QStandardItem>
#include <QWheelEvent>
#include <QItemSelectionModel>

class SortedTableItem: public QStandardItem
{

public:
    SortedTableItem(const QString& name, const QString& origin_name={} ): QStandardItem(name), _str(name.toStdString()) , _origin_name(origin_name.toStdString()) {}

    bool operator< (const SortedTableItem &other) const
    {
        return doj::alphanum_impl(this->_str.c_str(),
                                  other._str.c_str()) < 0;
    }

    bool is_custom_plot()
    {
      return !_origin_name.empty();
    }

    std::string get_origin_name()
    {
      return _origin_name;
    }

private:
    std::string _str;
    std::string _origin_name;
};


//-------------------------------------------------

FilterableListWidget::FilterableListWidget(const CustomPlotMap &mapped_math_plots,
                                           QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FilterableListWidget),
    _completer( new TreeModelCompleter(this) ),
    _custom_plots(mapped_math_plots),
    _point_size(9)
{
    ui->setupUi(this);
    ui->tableView->viewport()->installEventFilter( this );

    _model = new QStandardItemModel(0, 2, this);

    for(auto table_view: {ui->tableView, ui->tableViewCustom})
    {
        table_view->viewport()->installEventFilter( this );
        table_view->setModel( _model );
        table_view->horizontalHeader()->setStretchLastSection (true);
        table_view->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    }

    ui->widgetOptions->setVisible(false);

    ui->radioRegExp->setAutoExclusive(true);
    ui->radioContains->setAutoExclusive(true);
    ui->radioPrefix->setAutoExclusive(true);

    _completer->setCompletionMode( QCompleter::PopupCompletion );

    QSettings settings;

    QString active_filter = settings.value("FilterableListWidget.searchFilter").toString();
    if( active_filter == "radioRegExp"){

        ui->radioRegExp->setChecked(true);
    }
    else if( active_filter == "radioPrefix"){

        ui->radioPrefix->setChecked(true);
    }
    else if( active_filter == "radioContains"){

        ui->radioContains->setChecked(true);
    }

    _point_size = settings.value("FilterableListWidget/table_point_size", 9).toInt();

    _completer_need_update = ui->radioPrefix->isChecked();
    ui->lineEdit->setCompleter( _completer_need_update ? _completer : nullptr );

    ui->splitter->setStretchFactor(0,5);
    ui->splitter->setStretchFactor(1,1);

    connect(  ui->tableViewCustom->selectionModel(), &QItemSelectionModel::selectionChanged,
              this, &FilterableListWidget::onCustomSelectionChanged );
}

FilterableListWidget::~FilterableListWidget()
{
    delete ui;
}

int FilterableListWidget::rowCount() const
{
    return _model->rowCount();
}

void FilterableListWidget::clear()
{
    _model->setRowCount(0);
    _completer->clear();
    ui->labelNumberDisplayed->setText( "0 of 0");
}

void FilterableListWidget::addItem(const QString &item_name,const QString & origin_name)
{
    if( _model->findItems(item_name).size() > 0)
    {
        return;
    }

    auto item = new SortedTableItem(item_name, origin_name);
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSize(_point_size);
    item->setFont(font);
    const int row = rowCount();
    _model->setRowCount(row+1);
    _model->setItem(row, 0, item);

    auto val_cell = new QStandardItem("-");
    val_cell->setTextAlignment(Qt::AlignRight);
    val_cell->setFlags( Qt::NoItemFlags | Qt::ItemIsEnabled );
    font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize( _point_size );
    val_cell->setFont( font );
    val_cell->setFlags(Qt::NoItemFlags);

    _model->setItem(row, 1, val_cell );

    if( _completer_need_update )
    {
        _completer->addToCompletionTree(item_name);
    }
}

void FilterableListWidget::refreshColumns()
{
    ui->tableView->sortByColumn(0,Qt::AscendingOrder);
    ui->tableViewCustom->sortByColumn(0,Qt::AscendingOrder);

    ui->tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->tableViewCustom->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    updateFilter();
}


int FilterableListWidget::findRowByName(const std::string &text) const
{
    auto item_list = _model->findItems( QString::fromStdString( text ), Qt::MatchExactly);
    if( item_list.isEmpty())
    {
        return -1;
    }
    if( item_list.count()>1)
    {
        qDebug() << "FilterableListWidget constins multiple rows with the same name";
        return -1;
    }
    return item_list.front()->row();
}


void FilterableListWidget::updateFilter()
{
    on_lineEdit_textChanged( ui->lineEdit->text() );
}

void FilterableListWidget::keyPressEvent(QKeyEvent *event)
{
    if( event->key() == Qt::Key_Delete){
        removeSelectedCurves();
    }
}

bool FilterableListWidget::eventFilter(QObject *object, QEvent *event)
{
    auto obj = object;
    while ( obj && obj != ui->tableView && obj != ui->tableViewCustom )
    {
        obj = obj->parent();
    }

    //Ignore obj different than tableViews
    if(!obj)
    {
        return QWidget::eventFilter(object,event);
    }

    bool shift_modifier_pressed = (QGuiApplication::keyboardModifiers() == Qt::ShiftModifier);
    bool ctrl_modifier_pressed  = (QGuiApplication::keyboardModifiers() == Qt::ControlModifier);

    if(event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *mouse_event = static_cast<QMouseEvent*>(event);

        _dragging = false;
        _drag_start_pos = mouse_event->pos();

        if( !shift_modifier_pressed && !ctrl_modifier_pressed )
        {
            if( obj == ui->tableView)
            {
                ui->tableViewCustom->clearSelection() ;
            }
            if( obj == ui->tableViewCustom)
            {
                ui->tableView->clearSelection() ;
            }
        }

        if(mouse_event->button() == Qt::LeftButton )
        {
            _newX_modifier = false;
        }
        else if(mouse_event->button() == Qt::RightButton )
        {
            _newX_modifier = true;
        }
        else {
            return false;
        }
        return QWidget::eventFilter(object,event);
    }
    else if(event->type() == QEvent::MouseMove)
    {
        QMouseEvent *mouse_event = static_cast<QMouseEvent*>(event);
        double distance_from_click = (mouse_event->pos() - _drag_start_pos).manhattanLength();

        if ((mouse_event->buttons() == Qt::LeftButton ||
             mouse_event->buttons() == Qt::RightButton) &&
            distance_from_click >= QApplication::startDragDistance() &&
            !_dragging)
        {
            _dragging = true;
            QDrag *drag = new QDrag(this);
            QMimeData *mimeData = new QMimeData;

            QByteArray mdata;
            QDataStream stream(&mdata, QIODevice::WriteOnly);

            for(const auto& curve_name: getNonHiddenSelectedRows())
            {
                stream << QString::fromStdString(curve_name);
            }

            if( !_newX_modifier )
            {
                mimeData->setData("curveslist/add_curve", mdata);
            }
            else
            {
                if(getNonHiddenSelectedRows().size() != 1)
                {
                    return false;
                }
                mimeData->setData("curveslist/new_X_axis", mdata);

                QPixmap cursor( QSize(160,30) );
                cursor.fill(Qt::transparent);

                QPainter painter;
                painter.begin( &cursor);
                painter.setPen(QColor(22, 22, 22));

                QString text("set as new X axis");
                painter.setFont( QFont("Arial", 14 ) );

                painter.setBackground(Qt::transparent);
                painter.drawText( QRect(0, 0, 160, 30), Qt::AlignHCenter | Qt::AlignVCenter, text );
                painter.end();

                drag->setDragCursor(cursor, Qt::MoveAction);
            }

            drag->setMimeData(mimeData);
            drag->exec(Qt::CopyAction | Qt::MoveAction);
        }
        return true;
    }
    else if(event->type() == QEvent::Wheel)
    {
        QWheelEvent *wheel_event = dynamic_cast<QWheelEvent*>(event);
        int prev_size = _point_size;
        if( ctrl_modifier_pressed )
        {
            if( _point_size > 6 && wheel_event->delta() < 0 )
            {
                _point_size--;
            }
            else if( _point_size < 14 && wheel_event->delta() > 0 )
            {
                _point_size++;
            }
            if( _point_size != prev_size)
            {
                auto horizontal = ui->tableView->horizontalHeader();
                horizontal->setSectionResizeMode(0, QHeaderView::Fixed);
                horizontal->setSectionResizeMode(1, QHeaderView::Fixed);

                auto vertical = ui->tableView->verticalHeader();
                vertical->setSectionResizeMode(0, QHeaderView::Fixed);
                vertical->setSectionResizeMode(1, QHeaderView::Fixed);

                for (int row=0; row< rowCount(); row++)
                {
                    for (int col=0; col< 2; col++)
                    {
                        auto item = _model->item( row, col );
                        auto font = item->font();
                        font.setPointSize( _point_size );
                        item->setFont( font );
                    }
                }

                horizontal->setSectionResizeMode(0, QHeaderView::ResizeToContents);
                horizontal->setSectionResizeMode(1, QHeaderView::Stretch);
                vertical->setSectionResizeMode(0, QHeaderView::ResizeToContents);
                vertical->setSectionResizeMode(1, QHeaderView::ResizeToContents);

                QSettings settings;
                settings.setValue("FilterableListWidget/table_point_size", _point_size);
            }
            return true;
        }
    }

    return QWidget::eventFilter(object,event);
}

std::vector<std::string> FilterableListWidget::getNonHiddenSelectedRows(bool use_custom_names)
{
    std::vector<std::string> non_hidden_list;

    for(auto table_view: {ui->tableView, ui->tableViewCustom})
    {
        for (const auto &selected_index : table_view->selectionModel()->selectedRows(0))
        {
            if (!table_view->isRowHidden(selected_index.row()))
            {
                auto item = _model->item( selected_index.row(), 0 );
                if (!use_custom_names || table_view == ui->tableView)
                {
                  non_hidden_list.push_back(item->text().toStdString());
                }
                else
                {
                  non_hidden_list.push_back(item->text().toStdString());
                  non_hidden_list.push_back(static_cast<SortedTableItem*>(item)->get_origin_name());
                }

            }
        }
    }
    return non_hidden_list;
}

QTableView *FilterableListWidget::getTableView() const
{
    return ui->tableView;
}

QTableView *FilterableListWidget::getCustomView() const
{
    return ui->tableViewCustom;
}

void FilterableListWidget::on_radioContains_toggled(bool checked)
{
    if(checked) {
        updateFilter();
        ui->lineEdit->setCompleter( nullptr );
        QSettings settings;
        settings.setValue("FilterableListWidget.searchFilter", "radioContains");
    }
}

void FilterableListWidget::on_radioRegExp_toggled(bool checked)
{
    if(checked) {
        updateFilter();
        ui->lineEdit->setCompleter( nullptr );
        QSettings settings;
        settings.setValue("FilterableListWidget.searchFilter", "radioRegExp");
    }
}

void FilterableListWidget::on_radioPrefix_toggled(bool checked)
{
    _completer_need_update = checked;

    if( checked )
    {
        _completer->clear();
        for (int row=0; row< rowCount(); row++)
        {
            auto item = _model->item(row,0);
            _completer->addToCompletionTree(item->text());
        }

        updateFilter();
        ui->lineEdit->setCompleter( _completer );
        QSettings settings;
        settings.setValue("FilterableListWidget.searchFilter", "radioPrefix");
    }
}

void FilterableListWidget::on_checkBoxCaseSensitive_toggled(bool )
{
    updateFilter();
}

void FilterableListWidget::on_lineEdit_textChanged(const QString &search_string)
{
    int item_count = rowCount();
    int visible_count = 0;
    bool updated = false;

    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    if( ui->checkBoxCaseSensitive->isChecked())
    {
        cs = Qt::CaseSensitive;
    }
    QRegExp regexp( search_string,  cs, QRegExp::Wildcard );
    QRegExpValidator v(regexp, nullptr);

    QStringList spaced_items = search_string.split(' ');

    for (int row=0; row < rowCount(); row++)
    {
        auto item = _model->item(row,0);
        QString name = item->text();
        int pos = 0;
        bool toHide = false;

        if( search_string.isEmpty() == false )
        {
            if( ui->radioRegExp->isChecked())
            {
                toHide = v.validate( name, pos ) != QValidator::Acceptable;
            }
            else if( ui->radioPrefix->isChecked())
            {
                toHide = !name.startsWith( search_string, cs ) ;
            }
            else if( ui->radioContains->isChecked())
            {
                for (const auto& item: spaced_items)
                {
                    if( name.contains(item, cs) == false )
                    {
                        toHide = true;
                        break;
                    }
                }
            }
        }
        if( !toHide ) visible_count++;

        if( toHide != ui->tableView->isRowHidden(row) ) updated = true;

        const auto name_std = name.toStdString();
        const bool is_custom_plot = dynamic_cast <SortedTableItem*>(item)->is_custom_plot();


        ui->tableView->setRowHidden(row, toHide || is_custom_plot );
        ui->tableViewCustom->setRowHidden(row, toHide || !is_custom_plot );
    }
    ui->labelNumberDisplayed->setText( QString::number( visible_count ) + QString(" of ") +
                                       QString::number( item_count ) );

    if(updated){
        emit hiddenItemsChanged();
    }
}

void FilterableListWidget::on_pushButtonSettings_toggled(bool checked)
{
    ui->widgetOptions->setVisible(checked);
}

void FilterableListWidget::on_checkBoxHideSecondColumn_toggled(bool checked)
{
    for(auto table_view: {ui->tableView, ui->tableViewCustom})
    {
        if(checked){
            table_view->hideColumn(1);
        }
        else{
            table_view->showColumn(1);
        }
    }
    emit hiddenItemsChanged();
}

void FilterableListWidget::removeSelectedCurves()
{
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(nullptr, tr("Warning"),
                                  tr("Do you really want to remove these data?\n"),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No );

    if (reply == QMessageBox::Yes)
    {
        emit deleteCurves(getNonHiddenSelectedRows(true));
    }

    // rebuild the tree model
    if( _completer_need_update )
    {
        _completer->clear();
        for (int row=0; row< rowCount(); row++)
        {
            auto item = _model->item(row);
            _completer->addToCompletionTree(item->text());
        }
    }

    updateFilter();
}

void FilterableListWidget::removeRow(int row)
{
    _model->removeRow(row);
}

void FilterableListWidget::on_buttonAddCustom_clicked()
{
    auto curve_names = getNonHiddenSelectedRows();
    if( curve_names.empty() )
    {
        emit createMathPlot("");
    }
    else
    {
        createMathPlot( curve_names.front() );
    }
    on_lineEdit_textChanged( ui->lineEdit->text() );
}

//void FilterableListWidget::on_buttonRefreshAll_clicked()
//{
//    for(auto& it: _mapped_math_plots)
//    {
//        emit refreshMathPlot( it.second->name() );
//    }
//}

void FilterableListWidget::onCustomSelectionChanged(const QItemSelection&, const QItemSelection &)
{
    int selected_items = 0;

    for (const auto &index : ui->tableViewCustom->selectionModel()->selectedRows(0))
    {
        if (! ui->tableViewCustom->isRowHidden(index.row()) )
        {
            selected_items++;
        }
    }

    bool enabled = selected_items == 1;
    ui->buttonEditCustom->setEnabled( enabled );
    ui->buttonEditCustom->setToolTip( enabled ? "Edit the selected custom timeserie" :
                                                "Select a single custom Timeserie to Edit it");
}

void FilterableListWidget::on_buttonEditCustom_clicked()
{
    int selected_count = 0;
    QModelIndex selected_index;
    auto table_view = ui->tableViewCustom;

    for (QModelIndex index : table_view->selectionModel()->selectedRows(0))
    {
        if (!table_view->isRowHidden( index.row()) )
        {
            selected_count++;
            selected_index = index;
        }
    }
    if( selected_count != 1)
    {
        return;
    }

    auto item = _model->item( selected_index.row(), 0 );
    editMathPlot( static_cast<SortedTableItem*>(item)->get_origin_name() );
    on_lineEdit_textChanged( ui->lineEdit->text() );
}

void FilterableListWidget::clearSelections()
{
    ui->tableViewCustom->clearSelection();
    ui->tableView->clearSelection();
}
