# UI 与现有业务逻辑（`ThinkGearLinkTester`）对接 — 示例代码

> **说明**：本文**不修改**你工程里任何现有源文件；下面代码均为**可复制粘贴**的示例。  
> 你当前 UI 已有：`pushButton_start` / `pushButton_stop` / `pushButton_clear` / `pushButton_save`、`listWidget_picture`、`listWidget_text`、`statusbar`（见 `mainwindow.ui`）。  
> 业务对象已有信号：`ThinkGearLinkTester::secondReport`、`ThinkGearLinkTester::testMessage`（见 `thinkgearlinktester.h`）。

### CSV 记录（方案 A，仅文档示例）

- **开始前**由用户选择：**是否需要**把 `LogBuffer` 写入 CSV。  
- **需要**：文件落在**固定默认目录**（例如「文档/QtBCI_logs/」），**不再**弹完整「另存为」选路径对话框；每次开始可用时间戳生成新文件名，避免覆盖。  
- **不需要**：本次采集**不写 CSV**（不启动 `CsvLogWorker` 或 worker 空转不写文件——由你在 `ThinkGearLinkTester::start()` 内分支实现）。  
- 界面上：**显示当前 CSV 完整路径**；提供**快捷按钮**在资源管理器中定位/打开该文件（Windows 可用 `explorer /select,`）。

> 下文 `MainWindow` 示例假设你在 Designer 里**增加了**三个控件（对象名需与代码一致）：  
> - `checkBox_logCsv`（`QCheckBox`，文案如「保存 CSV 到默认目录」）  
> - `label_logCsvPath`（`QLabel`，可设 `wordWrap`、最小高度，用于显示路径）  
> - `pushButton_openLogCsv`（`QPushButton`，文案如「打开 CSV…」）

若你希望**不增加主界面控件**，而是点击 **`pushButton_save` 弹出对话框** 集中做 CSV 设置，见：**`docs/UI保存按钮_CSV设置对话框示例.md`**。

---

## 一、整体思路（线程与信号槽）

| 层级 | 做什么 |
|------|--------|
| **UI（`MainWindow`，主线程）** | 按钮控制开始/停止；把日志、统计刷到 `QListWidget` / `QStatusBar` |
| **业务（`ThinkGearLinkTester`）** | 串口 → 组帧 → 解析 → `PictureAndALgBuffer` / `LogBuffer` / `CsvLogWorker` |
| **连接方式** | 用 `connect` 把 `secondReport`、`testMessage` 接到 UI 槽；**不要**在 UI 里直接读串口 |

跨线程时 Qt 会自动排队（`Qt::AutoConnection`），一般把 `ThinkGearLinkTester` 放在**主线程**（`new ThinkGearLinkTester(this)`）最简单。

---

## 二、`main.cpp` 示例（窗口 + 业务对象）

把入口从「只跑 `ThinkGearLinkTester`」改成「显示 `MainWindow`，由窗口持有 tester」：

```cpp
#include <QApplication>
#include "mainwindow.h"
#include "thinkgear/thinkgearlinktester.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow w;

    // 业务对象生命周期交给窗口（窗口析构时一起释放）
    auto *tester = new ThinkGearLinkTester(&w);
    w.attachLinkTester(tester);   // 见下一节在 MainWindow 里增加的接口

    w.show();
    return app.exec();
}
```

---

## 三、`MainWindow` 头文件示例片段（`mainwindow.h`）

在现有类里**增加**前向声明与公开方法（保留你原来的 `Q_OBJECT` 与 `ui` 成员）：

```cpp
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class ThinkGearLinkTester;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    /** 由 main 创建 tester 后注入，完成信号槽绑定 */
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
};

#endif
```

> **命名槽**：`on_pushButton_start_clicked` 等名字与 Qt Designer 的 **“转到槽”** 一致时，可与 `QMetaObject::connectSlotsByName` 配合；若你更喜欢手写 `connect`，槽名可任意。

---

## 四、`MainWindow` 实现示例（`mainwindow.cpp`）

```cpp
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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    updateLogCsvPathUi();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::attachLinkTester(ThinkGearLinkTester *tester)
{
    m_tester = tester;
    setupTesterConnections();
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
    const QString root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString sub = QStringLiteral("QtBCI_logs");
    const QString dirPath = root + QLatinChar('/') + sub;
    QDir().mkpath(dirPath);

    const QString name = QStringLiteral("raw_%1.csv")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss")));
    return QFileInfo(dirPath, name).absoluteFilePath();
}

void MainWindow::updateLogCsvPathUi()
{
    if (!ui->label_logCsvPath)
        return;
    if (m_currentCsvPath.isEmpty()) {
        ui->label_logCsvPath->setText(QStringLiteral("当前 CSV：未启用（本次不写入文件）"));
        if (ui->pushButton_openLogCsv)
            ui->pushButton_openLogCsv->setEnabled(false);
    } else {
        ui->label_logCsvPath->setText(QStringLiteral("当前 CSV：%1").arg(m_currentCsvPath));
        if (ui->pushButton_openLogCsv)
            ui->pushButton_openLogCsv->setEnabled(QFile::exists(m_currentCsvPath));
    }
}

void MainWindow::on_pushButton_start_clicked()
{
    if (!m_tester) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("未绑定 ThinkGearLinkTester"));
        return;
    }

    // ---------- 1) 是否写 CSV：仅勾选框，路径固定为默认目录 ----------
    const bool wantCsv = ui->checkBox_logCsv && ui->checkBox_logCsv->isChecked();
    if (wantCsv) {
        m_currentCsvPath = makeDefaultCsvPath();
        // 业务层必须实现：见下文「ThinkGearLinkTester 最小配合接口」
        m_tester->setCsvLoggingEnabled(true);
        m_tester->setCsvOutputPath(m_currentCsvPath);
    } else {
        m_currentCsvPath.clear();
        m_tester->setCsvLoggingEnabled(false);
    }
    updateLogCsvPathUi();

    // 端口可改为：下拉框枚举 QSerialPortInfo::availablePorts()
    bool ok = false;
    const QString port = QInputDialog::getText(
        this,
        QStringLiteral("串口"),
        QStringLiteral("端口名（如 COM7）:"),
        QLineEdit::Normal,
        QStringLiteral("COM7"),
        &ok);
    if (!ok || port.trimmed().isEmpty())
        return;

    m_tester->setTargetRawPerSec(256);
    m_tester->setTolerance(25);

    const bool started = m_tester->start(port.trimmed(), 57600);
    if (started) {
        ui->statusbar->showMessage(QStringLiteral("已打开 %1 @57600").arg(port.trimmed()), 5000);
        // 文件可能尚未创建：open 按钮在首次 drain 写入后再 enable，可连接 CsvLogWorker::workerInfo 刷新
        updateLogCsvPathUi();
    }
}

void MainWindow::on_pushButton_stop_clicked()
{
    if (m_tester)
        m_tester->stop();
    ui->statusbar->showMessage(QStringLiteral("已停止"), 3000);
}

void MainWindow::on_pushButton_clear_clicked()
{
    ui->listWidget_text->clear();
    ui->listWidget_picture->clear();
}

void MainWindow::on_pushButton_save_clicked()
{
    // 仍示例：导出界面列表为 .txt（与 CSV 无冲突）；若你希望「保存」只做 CSV，可改成调用 on_pushButton_openLogCsv_clicked
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存"),
        QStringLiteral("report_%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        QStringLiteral("文本 (*.txt);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("错误"), f.errorString());
        return;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    for (int i = 0; i < ui->listWidget_text->count(); ++i)
        out << ui->listWidget_text->item(i)->text() << '\n';
}

void MainWindow::on_pushButton_openLogCsv_clicked()
{
    if (m_currentCsvPath.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("当前未启用 CSV 记录。"));
        return;
    }
    if (!QFile::exists(m_currentCsvPath)) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("文件尚未生成或已被移动：\n%1").arg(m_currentCsvPath));
        return;
    }

#if defined(Q_OS_WIN)
    const QString native = QDir::toNativeSeparators(m_currentCsvPath);
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), native});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_currentCsvPath).absolutePath()));
#endif
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
    ui->listWidget_text->scrollToBottom();

    ui->statusbar->showMessage(line, 1000);
}

void MainWindow::onTestMessage(const QString &msg)
{
    ui->listWidget_text->addItem(msg);
    ui->listWidget_text->scrollToBottom();
}
```

**注意**：示例里已在 `mainwindow.cpp` 顶部列出常用 `#include`；若你拆文件请自行补齐。

---

## 五、`listWidget_picture`（曲线/图像区）怎么接？

你现在的 `ThinkGearLinkTester` 里 **`m_pictureandalgBuffer` 是私有的**，UI **无法直接** `window()` 去读缓冲。

可选三种做法（按推荐顺序）：

### 方案 A（推荐）：在业务类里发信号

在 `ThinkGearLinkTester` 中增加例如：

```cpp
signals:
    void plotChunkReady(const QVector<RawSample> &chunk); // 或只发最后一个点
```

在 `onTick1s()` 或 `onRawUvReady()` 里 `emit`（注意频率，建议与绘图刷新一致，如 30Hz）。

`MainWindow` 里 `connect` 到槽，更新 `QCustomPlot` 或自建 `QWidget` 绘图。

### 方案 B：提供只读访问接口

```cpp
public:
    PictureAndALgBuffer &pictureBuffer() { return m_pictureandalgBuffer; }
```

`MainWindow` 里用 `QTimer`（如 33ms）定时 `window_RawSample()` 或 `tryDequeueRawChunkForPlot`，**不要**每点刷新 UI。

### 方案 C：暂时只用 `secondReport`

先不接曲线，只接文本与状态栏；等业务稳定后再做 A/B。

---

## 六、与 `CsvLogWorker` 的关系（方案 A：默认路径 + 可关闭）

上面 `MainWindow::on_pushButton_start_clicked()` 调用了两个**需要你自行在 `ThinkGearLinkTester` 中实现**的方法（示例签名）：

```cpp
// thinkgearlinktester.h（示例，由你合并进真实类）
public:
    void setCsvLoggingEnabled(bool on);
    void setCsvOutputPath(const QString &absPath);
```

**语义约定**：

- `setCsvLoggingEnabled(false)`：`start()` 时**不要**创建/启动 `CsvLogWorker`，或 worker 不写文件；`LogBuffer::push` 可保留或也跳过（二选一，按你是否还要内存日志）。  
- `setCsvLoggingEnabled(true)`：`start()` 里对 `CsvLogWorker` 调用 `setOutputPath(absPath)` 再 `moveToThread` / `start`，与现有 `CSV日志写盘线程示例_CsvLogWorker.md` 一致。  
- `setCsvOutputPath`：仅在 `enabled==true` 时有意义；路径为**绝对路径**，由 UI 的 `makeDefaultCsvPath()` 生成。

**UI 与路径显示**：

- `label_logCsvPath` 每次勾选状态变化或开始采集后调用 `updateLogCsvPathUi()`。  
- `pushButton_openLogCsv` 使用 Windows `explorer /select,` 定位文件；非 Windows 退化为打开所在目录（你可改为 `QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentCsvPath))` 直接用默认程序打开 CSV）。

**可选**：连接 `CsvLogWorker::workerInfo` 或 `drained`，在首次创建文件后再次 `updateLogCsvPathUi()`，以便按钮及时可点。

---

## 七、对接前请自检（避免“接了 UI 却不工作”）

1. **`main.cpp` 是否仍直接 `start("COM7")` 且不显示窗口**  
   若如此，应改为上一节的 `MainWindow` 启动方式。

2. **`ThinkGearLinkTester::start()` 里是否在打开串口前误调 `stop()`**  
   若 `stop()` 会 `quit` 日志线程，可能导致写盘线程起不来或反复重建；应用 `start()` 只做「关旧串口 + 清缓冲 + 开新串口」，**不要**在成功路径上无条件 `stop()` 整个链路（以你当前实现为准，自行对照）。

3. **Designer 的“转到槽”**  
   若使用 `on_pushButton_*_clicked`，需在 Qt Creator 里为按钮生成槽，或改用手写 `connect`。

---

## 八、最小验收

1. 勾选/不勾选「保存 CSV」→ `label_logCsvPath` 分别显示默认路径或「不写入文件」。  
2. 点「开始」→ 状态栏提示端口；需要 CSV 时 `Documents/QtBCI_logs/raw_时间戳.csv` 应最终被创建（视 worker 何时首次 flush）。  
3. 「打开 CSV…」→ 资源管理器跳到该文件（Windows）。  
4. 每秒 `secondReport` 仍出现在列表；「停止」正常；「保存」仍可导出列表 txt（若保留该示例）。

---

*文档版本：含 CSV 方案 A（默认目录、可关闭、路径显示、快捷打开）；`ThinkGearLinkTester` 侧接口需你自行合并实现。*

---

**与当前工程逐步对齐的合并版（含 `start()` 误调 `stop()` 等问题的改法）见：`docs/完整对接_MainWindow与CSV_逐步代码.md`。**
