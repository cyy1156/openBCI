#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include<thinkgear/thinkgearlinktester.h>
#include<QFile>
#include<QTextStream>
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void attachLinkTester(ThinkGearLinkTester *tester);

private slots:
    void on_pushButton_start_clicked();
    void on_pushButton_stop_clicked();
    void on_pushButton_clear_clicked();
    void on_pushButton_save_clicked();
    /** 快捷打开当前 CSV（或所在文件夹） */
    void on_pushButton_openLogCsv_clicked();
    void onSecondReport(quint64 secIndex, int rawPerSec, int framePerSec, int warnPerSec, bool pass);
    void onTestMessage(const QString &msg);

private:
    void setupTesterConnections();
    /** 默认目录下的 CSV 路径（每次「开始」可生成新文件名） */
    static QString makeDefaultCsvPath();
    void updateLogCsvPathUi();

    Ui::MainWindow *ui;
    ThinkGearLinkTester *m_tester = nullptr;
    /** 最近一次为「写 CSV」生成的绝对路径；不保存时为空 */
    QString m_currentCsvPath;
    bool m_csvLoggingEnable =false;
    bool m_uiTxtLoggingEnabled = true;
    // EEG 文件路径（传给 ThinkGearLinkTester/CsvLogWorker）
    QString m_eegCsvPath;
    // 操作日志 txt 路径（MainWindow 自己写）
    QString m_uiTxtPath;

    static QString makeDefaultEegCsvPath();
    static QString makeDefaultUiTxtPath();
    void updateSavePathUi();
    void appendUiActionLog(const QString &category, const QString &message);

};
#endif // MAINWINDOW_H
