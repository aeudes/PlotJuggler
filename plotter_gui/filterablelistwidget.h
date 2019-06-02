#ifndef CURVE_SELECTOR_H
#define CURVE_SELECTOR_H

#include <QWidget>
#include <QAction>
#include <QListWidget>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QTableView>
#include <QItemSelection>

#include "transforms/custom_function.h"
#include "tree_completer.h"

class SortedTableItem;

namespace Ui {
class FilterableListWidget;
}

class FilterableListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FilterableListWidget(const CustomPlotMap& mapped_math_plots,
                                  QWidget *parent);

    ~FilterableListWidget() override;

    int rowCount() const;

    void clear();

    void addItem(const QString& item_name, const QString& origin_name={});

    void refreshColumns();

    int findRowByName(const std::string &text) const;

    void removeRow(int row);

    void rebuildEntireList(const std::vector<std::string> &names);

    void updateFilter();

    QStandardItemModel *getTableModel() const
    {
        return _model;
    }

    QTableView* getTableView() const;

    QTableView* getCustomView() const;

    bool is2ndColumnHidden() const
    {
        return getTableView()->isColumnHidden(1);
    }

    virtual void keyPressEvent(QKeyEvent * event) override;

private slots:

    void on_radioContains_toggled(bool checked);

    void on_radioRegExp_toggled(bool checked);

    void on_radioPrefix_toggled(bool checked);

    void on_checkBoxCaseSensitive_toggled(bool checked);

    void on_lineEdit_textChanged(const QString &search_string);

    void on_pushButtonSettings_toggled(bool checked);

    void on_checkBoxHideSecondColumn_toggled(bool checked);

    void removeSelectedCurves();

    void on_buttonAddCustom_clicked();

    void on_buttonEditCustom_clicked();

    void onCustomSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);

public slots:

    void clearSelections();

private:

    Ui::FilterableListWidget *ui;

    QPoint _drag_start_pos;

    bool _newX_modifier, _dragging;

    TreeModelCompleter* _completer;

    bool eventFilter(QObject *object, QEvent *event) override;

    void updateTreeModel();

    std::vector<std::string> getNonHiddenSelectedRows(bool use_custom_names = false);

    bool _completer_need_update;

    QStandardItemModel* _model;

    const CustomPlotMap& _custom_plots;

    int _point_size;
signals:

    void hiddenItemsChanged();

    void createMathPlot(const std::string& linked_plot);
    void editMathPlot(const std::string& plot_name);
    void refreshMathPlot(const std::string& curve_name);

    void deleteCurves(const std::vector<std::string>& curve_names);


};

#endif // CURVE_SELECTOR_H
