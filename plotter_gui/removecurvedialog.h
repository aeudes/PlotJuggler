#ifndef REMOVECURVEDIALOG_H
#define REMOVECURVEDIALOG_H

#include <QDialog>
#include <QListWidgetItem>
#include <qwt_plot_curve.h>

namespace Ui {
class RemoveCurveDialog;
}

class RemoveCurveDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RemoveCurveDialog(QWidget *parent);
    ~RemoveCurveDialog();

    void addCurveName(const QString& name, const QColor& color=Qt::black);

private slots:
    void on_listCurveWidget_itemClicked(QListWidgetItem *item);

    void on_pushButtonRemove_pressed();

    void on_pushButtonSelectAll_pressed();

    void on_pushButtonClear_pressed();

private:
    Ui::RemoveCurveDialog *ui;

    void setAll(bool state);
};

#endif // REMOVECURVEDIALOG_H
