# 实现示例：脑电只存 CSV、操作只存 TXT（结合你当前 QtBCI 代码）

> 说明：本文是**修改示例**，不直接改你项目源码。  
> 目标：  
> 1) EEG 数据（`tsMs,seq,rawInt16,rawUv`）只写 CSV。  
> 2) UI 操作/系统提示只写 TXT。  
> 3) `保存` 对话框管理两个路径：当前 EEG CSV、当前操作 TXT。  

---

## 1) `mainwindow.h` 修改示例

在你现有 `MainWindow` 类里增加以下成员/方法（保留你已有的声明）：

```cpp
// 新增 include
#include <QFile>
#include <QTextStream>

private:
    // EEG 文件路径（传给 ThinkGearLinkTester/CsvLogWorker）
    QString m_eegCsvPath;
    // 操作日志 txt 路径（MainWindow 自己写）
    QString m_uiTxtPath;

    // 已有字段建议统一命名
    bool m_csvLoggingEnabled = false; // 你当前是 m_csvLoggingEnable，二选一统一

    static QString makeDefaultEegCsvPath();
    static QString makeDefaultUiTxtPath();
    void updateSavePathUi();
    void appendUiActionLog(const QString &category, const QString &message);
```

---

## 2) `mainwindow.cpp` 顶部 include 示例

```cpp
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialog>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QStringConverter>
```

---

## 3) 默认路径函数

```cpp
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
```

---

## 4) 统一 UI 状态展示（状态栏）

```cpp
void MainWindow::updateSavePathUi()
{
    const QString eeg = m_eegCsvPath.isEmpty() ? QStringLiteral("未设置") : m_eegCsvPath;
    const QString txt = m_uiTxtPath.isEmpty() ? QStringLiteral("未设置") : m_uiTxtPath;
    ui->statusbar->showMessage(QStringLiteral("EEG CSV: %1 | UI TXT: %2").arg(eeg, txt), 5000);
}
```

---

## 5) 操作日志写 TXT（核心）

```cpp
void MainWindow::appendUiActionLog(const QString &category, const QString &message)
{
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
```

---

## 6) `on_pushButton_save_clicked()` 对话框示例（双路径）

> 这个版本去掉“格式三选一”，固定：EEG=CSV，操作=TXT。

```cpp
void MainWindow::on_pushButton_save_clicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("保存配置"));
    dlg.setMinimumWidth(520);
    auto *lay = new QVBoxLayout(&dlg);

    auto *cbEnableCsv = new QCheckBox(QStringLiteral("开始采集时启用 EEG CSV 保存"), &dlg);
    cbEnableCsv->setChecked(m_csvLoggingEnabled);

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

    m_csvLoggingEnabled = cbEnableCsv->isChecked();
    if (m_csvLoggingEnabled && m_eegCsvPath.isEmpty())
        m_eegCsvPath = makeDefaultEegCsvPath();
    if (m_uiTxtPath.isEmpty())
        m_uiTxtPath = makeDefaultUiTxtPath();

    updateSavePathUi();
    appendUiActionLog(QStringLiteral("UI"), QStringLiteral("保存配置：csvEnable=%1, eeg=%2, txt=%3")
                      .arg(m_csvLoggingEnabled).arg(m_eegCsvPath, m_uiTxtPath));
}
```

---

## 7) `on_pushButton_start_clicked()` 关键段（把 EEG CSV 传给 tester）

```cpp
void MainWindow::on_pushButton_start_clicked()
{
    if (!m_tester) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("未绑定 ThinkGearLinkTester"));
        return;
    }

    // 固定策略：EEG 只 CSV
    m_tester->setCsvLoggingEnabled(m_csvLoggingEnabled);
    if (m_csvLoggingEnabled) {
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

    // 下面保留你现有串口输入/启动逻辑...
}
```

---

## 8) 把 UI/系统事件写入 TXT

在这些函数里追加 `appendUiActionLog(...)`：

```cpp
void MainWindow::on_pushButton_stop_clicked()
{
    if (m_tester)
        m_tester->stop();
    appendUiActionLog(QStringLiteral("UI"), QStringLiteral("点击停止"));
}

void MainWindow::on_pushButton_clear_clicked()
{
    ui->listWidget_text->clear();
    ui->listWidget_picture->clear();
    appendUiActionLog(QStringLiteral("UI"), QStringLiteral("点击清除"));
}

void MainWindow::onSecondReport(quint64 secIndex, int rawPerSec, int framePerSec, int warnPerSec, bool pass)
{
    const QString line = QStringLiteral("[%1] raw/s=%2 frame/s=%3 warn/s=%4 pass=%5")
                             .arg(secIndex).arg(rawPerSec).arg(framePerSec).arg(warnPerSec)
                             .arg(pass ? QStringLiteral("OK") : QStringLiteral("NO"));
    ui->listWidget_text->addItem(line);
    ui->listWidget_text->scrollToBottom();
    appendUiActionLog(QStringLiteral("STAT"), line);
}

void MainWindow::onTestMessage(const QString &msg)
{
    ui->listWidget_text->addItem(msg);
    ui->listWidget_text->scrollToBottom();
    appendUiActionLog(QStringLiteral("SYS"), msg);
}
```

---

## 9) `ThinkGearLinkTester` 侧注意（你现有代码基本已具备）

你已实现：

- `setCsvLoggingEnabled(bool)`
- `setCsvOutputPath(QString)`
- `startupLogWriter()` / `shutdownLogWriter()`

只需保证：

1. `start()` 中在 `m_csvLoggingEnabled==true` 时调用 `startupLogWriter()`
2. `m_csvOutputPath` 不为空
3. 修复你之前出现过的 `QObject::startTimer...` 跨线程问题（见你已有文档）

---

## 10) 验收标准

1. 点击保存配置后，能看到 EEG CSV 与操作 TXT 两条路径。  
2. 点击开始后，EEG CSV 有数据行（不仅表头）。  
3. 操作 TXT 会写入 `UI/SYS/STAT` 三类行。  
4. 点击停止/清除会继续追加到操作 TXT。  

