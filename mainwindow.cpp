#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "thinkgear/thinkgearlinktester.h"

#include <QDateTime>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QDialog>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}
void MainWindow::setupTesterConnections()
{
    if (!m_tester)
        return;

    // 每秒统计 → 状态栏 + 文本列表（节流：每秒一次，安全）
    connect(m_tester, &ThinkGearLinkTester::secondReport,
            this, &MainWindow::onSecondReport);

    connect(m_tester, &ThinkGearLinkTester::testMessage,
            this, &MainWindow::onTestMessage);

    // 若不用“转到槽”，可改成显式 connect 按钮：
    // connect(ui->pushButton_start, &QPushButton::clicked, this, &MainWindow::on_pushButton_start_clicked);
}
QString MainWindow::makeDefaultCsvPath()
{
    const QString root=QStandardPaths::writableLocation(
        QStandardPaths::DocumentsLocation);
    const QString sub =QStringLiteral("QtBCI_logs");
    const QString dirPath =root+QLatin1Char('/')+sub;
    QDir().mkpath(dirPath);
    const QString name=QStringLiteral("raw_%1.csv").arg
                         (QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss")));
    return QFileInfo(dirPath,name).absoluteFilePath();
}
QString MainWindow::makeDefaultEegCsvPath()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString dirPath = root + QLatin1Char('/') + QStringLiteral("QtBCI_logs");
    QDir().mkpath(dirPath);
    const QString name = QStringLiteral("eeg_%1.csv")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    return QFileInfo(dirPath, name).absoluteFilePath();
}

QString MainWindow::makeDefaultUiTxtPath()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString dirPath = root + QLatin1Char('/') + QStringLiteral("QtBCI_logs");
    QDir().mkpath(dirPath);
    const QString name = QStringLiteral("ui_%1.txt")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    return QFileInfo(dirPath, name).absoluteFilePath();
}
void MainWindow::updateSavePathUi()
{
    const QString eeg = m_eegCsvPath.isEmpty() ? QStringLiteral("未设置") : m_eegCsvPath;
    const QString txt = m_uiTxtPath.isEmpty() ? QStringLiteral("未设置") : m_uiTxtPath;
    ui->statusbar->showMessage(
        QStringLiteral("EEG保存:%1 | 操作TXT:%2 | EEG CSV:%3 | UI TXT:%4")
            .arg(m_csvLoggingEnable ? QStringLiteral("开") : QStringLiteral("关"))
            .arg(m_uiTxtLoggingEnabled ? QStringLiteral("开") : QStringLiteral("关"))
            .arg(eeg)
            .arg(txt),
        5000);
}
void MainWindow::appendUiActionLog(const QString &category, const QString &message)
{
    if (!m_uiTxtLoggingEnabled)
        return;
    if (m_uiTxtPath.isEmpty())
        return;

    QFile f(m_uiTxtPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    out << "[" << ts << "] "
        << "[" << category << "] "
        << message << '\n';
}
void MainWindow::attachLinkTester(ThinkGearLinkTester *tester)
{
    m_tester=tester;
    setupTesterConnections();
}
void MainWindow::updateLogCsvPathUi()
{
    if(m_currentCsvPath.isEmpty())
        ui->statusbar->showMessage(QStringLiteral("CSV:本次未启用"),3000);
    else
        ui->statusbar->showMessage(QStringLiteral("CSV: %1").arg(m_currentCsvPath),5000);


}
void MainWindow::on_pushButton_start_clicked()
{
    if (!m_tester) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("未绑定 ThinkGearLinkTester"));
        return;
    }

    m_tester->setCsvLoggingEnabled(m_csvLoggingEnable);
    if (m_csvLoggingEnable) {
        if (m_eegCsvPath.isEmpty())
            m_eegCsvPath = makeDefaultEegCsvPath();
        m_tester->setCsvOutputPath(m_eegCsvPath);
    } else {
        m_tester->setCsvOutputPath(QString());
    }

    // 操作日志 TXT
    if (m_uiTxtPath.isEmpty())
        m_uiTxtPath = makeDefaultUiTxtPath();
    appendUiActionLog(QStringLiteral("UI"), QStringLiteral("点击开始"));
   // updateLogCsvPathUi();
    updateSavePathUi();

    bool ok =false;
    const QString port =QInputDialog::getText(
        this,
        QStringLiteral("串口"),
        QStringLiteral("端口名（如 COM7）:"),
        QLineEdit::Normal,
        QStringLiteral("COM7"),
        &ok);
    if(!ok||port.trimmed().isEmpty())
        return;
    m_tester->setTargetRawPerSec(256);
    m_tester->setTolerance(25);
    const bool started =m_tester->start(port.trimmed(),57600);
    if(started)
    {
        ui->statusbar->showMessage(QStringLiteral("已打开 %1 @57600").arg(port.trimmed()), 5000);

    }
}
void MainWindow::on_pushButton_stop_clicked()
{
    if(m_tester)
        m_tester->stop();
    //状态栏提醒3s之后自动消失
    ui->statusbar->showMessage(QStringLiteral("已经停止"),3000);
    appendUiActionLog(QStringLiteral("UI"), QStringLiteral("点击停止"));
}
void MainWindow::on_pushButton_save_clicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("保存配置"));
    dlg.setMinimumWidth(520);
    auto *lay = new QVBoxLayout(&dlg);

    auto *cbEnableCsv = new QCheckBox(QStringLiteral("开始采集时启用 EEG CSV 保存"), &dlg);
    cbEnableCsv->setChecked(m_csvLoggingEnable);
    auto *cbEnableUiTxt = new QCheckBox(QStringLiteral("启用操作日志 TXT 保存"), &dlg);
    cbEnableUiTxt->setChecked(m_uiTxtLoggingEnabled);

    auto *labEeg = new QLabel(QStringLiteral("EEG CSV：\n%1")
                                  .arg(m_eegCsvPath.isEmpty() ? QStringLiteral("未设置") : m_eegCsvPath), &dlg);
    labEeg->setWordWrap(true);
    auto *labTxt = new QLabel(QStringLiteral("操作 TXT：\n%1")
                                  .arg(m_uiTxtPath.isEmpty() ? QStringLiteral("未设置") : m_uiTxtPath), &dlg);
    labTxt->setWordWrap(true);

    auto *btnNewEeg = new QPushButton(QStringLiteral("新建 EEG CSV"), &dlg);
    auto *btnOpenEeg = new QPushButton(QStringLiteral("打开 EEG CSV"), &dlg);
    auto *btnNewTxt = new QPushButton(QStringLiteral("新建 操作TXT"), &dlg);
    auto *btnOpenTxt = new QPushButton(QStringLiteral("打开 操作TXT"), &dlg);

    auto refresh = [&]() {
        labEeg->setText(QStringLiteral("EEG CSV：\n%1")
                            .arg(m_eegCsvPath.isEmpty() ? QStringLiteral("未设置") : m_eegCsvPath));
        labTxt->setText(QStringLiteral("操作 TXT：\n%1")
                            .arg(m_uiTxtPath.isEmpty() ? QStringLiteral("未设置") : m_uiTxtPath));
    };

    connect(btnNewEeg, &QPushButton::clicked, &dlg, [&]() {
        m_eegCsvPath = makeDefaultEegCsvPath();
        refresh();
    });
    connect(btnNewTxt, &QPushButton::clicked, &dlg, [&]() {
        m_uiTxtPath = makeDefaultUiTxtPath();
        refresh();
    });

    connect(btnOpenEeg, &QPushButton::clicked, &dlg, [&]() {
        if (m_eegCsvPath.isEmpty() || !QFileInfo::exists(m_eegCsvPath)) {
            QMessageBox::information(&dlg, QStringLiteral("提示"), QStringLiteral("EEG CSV 文件不存在。"));
            return;
        }
#if defined(Q_OS_WIN)
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(m_eegCsvPath)});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_eegCsvPath));
#endif
    });

    connect(btnOpenTxt, &QPushButton::clicked, &dlg, [&]() {
        if (m_uiTxtPath.isEmpty() || !QFileInfo::exists(m_uiTxtPath)) {
            QMessageBox::information(&dlg, QStringLiteral("提示"), QStringLiteral("操作 TXT 文件不存在。"));
            return;
        }
#if defined(Q_OS_WIN)
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(m_uiTxtPath)});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_uiTxtPath));
#endif
    });

    lay->addWidget(cbEnableCsv);
    lay->addWidget(cbEnableUiTxt);
    lay->addWidget(labEeg);
    lay->addWidget(btnNewEeg);
    lay->addWidget(btnOpenEeg);
    lay->addSpacing(8);
    lay->addWidget(labTxt);
    lay->addWidget(btnNewTxt);
    lay->addWidget(btnOpenTxt);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return;

    m_csvLoggingEnable = cbEnableCsv->isChecked();
    m_uiTxtLoggingEnabled = cbEnableUiTxt->isChecked();
    if (m_csvLoggingEnable && m_eegCsvPath.isEmpty())
        m_eegCsvPath = makeDefaultEegCsvPath();
    if (m_uiTxtPath.isEmpty())
        m_uiTxtPath = makeDefaultUiTxtPath();

    updateSavePathUi();
    appendUiActionLog(QStringLiteral("UI"), QStringLiteral("保存配置：csvEnable=%1, eeg=%2, txt=%3")
                                                .arg(m_csvLoggingEnable).arg(m_eegCsvPath, m_uiTxtPath));

}
void MainWindow::onSecondReport(quint64 secIndex, int rawPerSec, int framePerSec, int warnPerSec, bool pass)
{
    const QString line = QStringLiteral("[%1] raw/s=%2 frame/s=%3 warn/s=%4 pass=%5")
    .arg(secIndex)
        .arg(rawPerSec)
        .arg(framePerSec)
        .arg(warnPerSec)
        .arg(pass ? QStringLiteral("OK") : QStringLiteral("NO"));
    ui->listWidget_text->addItem(line);
    ui->listWidget_text->scrollToBottom();//自动滚到最下面，保证你看到最新一行
    ui->statusbar->showMessage(line, 1000);//状态栏临时显示 1 秒。
    ui->listWidget_text->scrollToBottom();
    appendUiActionLog(QStringLiteral("STAT"), line);
}

void MainWindow::onTestMessage(const QString &msg)
{
    ui->listWidget_text->addItem(msg);
    ui->listWidget_text->scrollToBottom();
     appendUiActionLog(QStringLiteral("SYS"), msg);
}
void MainWindow::on_pushButton_clear_clicked()
{
    ui->listWidget_text->clear();
    ui->listWidget_picture->clear();
    appendUiActionLog(QStringLiteral("UI"), QStringLiteral("点击清除"));
}
void MainWindow::on_pushButton_openLogCsv_clicked()
{
    // 先留空也可以，至少先通过链接
}



















