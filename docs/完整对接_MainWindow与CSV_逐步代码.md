# 完整对接：`MainWindow` +「保存」对话框 + `ThinkGearLinkTester` + `CsvLogWorker`

> 本文根据你**当前仓库**中的真实文件整理：  
> `core/csvlogworker.{h,cpp}`、`mainwindow.{h,cpp}`、`thinkgear/thinkgearlinktester.{h,cpp}`，以及 `docs/UI保存按钮_CSV设置对话框示例.md` / `docs/UI与ThinkGear业务层对接示例.md` 的设计目标。  
> **下文是「建议你改成什么样」的完整示例**；请自行复制到对应 `.h/.cpp`（或对照修改）。

---

## 〇、你现有代码里必须先知道的 3 个问题

### 0.1 `ThinkGearLinkTester::start()` 里调用了 `stop()`（严重）

`thinkgearlinktester.cpp` 当前顺序大致是：

1. `m_logThread->start();`
2. **`stop();`**  ← 会 `quit` 日志线程并把 `m_logThread` / `m_logWorker` 置空  

结果是：**写盘线程刚启动就被停掉**，`logQ` 容易堆满。  

**改法**：从 `start()` 成功路径里**删掉**这句 `stop()`；若需要「重开串口前先关串口」，只调用 `m_serial->close()`，不要整包 `stop()`（`stop()` 会动日志线程）。

### 0.2 `MainWindow::makeDefaultCsvPath()` 文件名笔误

你现在是 `raw_1%.csv`，应为 **`raw_%1.csv`**（`%1` 是 `arg(时间)` 的占位符）。

### 0.3 `MainWindow` 里 `m_csvLoggingEnable` 未接入 `start()`

头文件里有 `m_csvLoggingEnable`，但 `on_pushButton_start_clicked` **没有**根据它调用 `setCsvLoggingEnabled` / 设置路径；`on_pushButton_save_clicked` 也**未完成**（`getSaveFileName` 后没有写文件逻辑）。

下文一并补齐。

---

## 一、`ThinkGearLinkTester`：头文件应增加的接口与成员

**文件**：`thinkgear/thinkgearlinktester.h`  

在 `public:` 里 `setTolerance` 后面增加：

```cpp
    /** 是否在采集时启动 CsvLogWorker；与 MainWindow 对话框勾选一致 */
    void setCsvLoggingEnabled(bool on) { m_csvLoggingEnabled = on; }
    bool csvLoggingEnabled() const { return m_csvLoggingEnabled; }

    /** 绝对路径；仅在 m_csvLoggingEnabled==true 时有效；在 start() 创建 worker 前设置 */
    void setCsvOutputPath(const QString &absPath) { m_csvOutputPath = absPath; }
    QString csvOutputPath() const { return m_csvOutputPath; }
```

在 `private:` 里（例如 `m_logWorker` 附近）增加：

```cpp
    bool m_csvLoggingEnabled = false;
    QString m_csvOutputPath;

    void shutdownLogWriter();   // 停定时器式 stop + quit 线程（原 stop() 里日志部分）
    void startupLogWriter();    // 按 m_csvLoggingEnabled 与 m_csvOutputPath 创建线程并 start worker
```

---

## 二、`ThinkGearLinkTester::start()` / `stop()` 推荐实现逻辑（替换要点）

**原则**：

- `start()`：**禁止**在打开串口前调用整包 `stop()`**（避免杀掉日志线程）**。  
- 若仅重开串口：`if (m_serial->isOpen()) m_serial->close();`  
- 日志线程：若 `m_csvLoggingEnabled`，先 `shutdownLogWriter()` 再 `startupLogWriter()`（每次采集新文件）；若 false，则 `shutdownLogWriter()` 即可。  
- `stop()`：关串口、停 `m_tick`、**`shutdownLogWriter()`**。

### 2.1 `startupLogWriter()` 示例（`thinkgearlinktester.cpp` 内新增）

```cpp
void ThinkGearLinkTester::startupLogWriter()
{
    if (!m_csvLoggingEnabled)
        return;
    if (m_csvOutputPath.trimmed().isEmpty()) {
        emit testMessage(QStringLiteral("已勾选 CSV 但未设置输出路径"));
        return;
    }
    if (m_logThread)
        return; // 已在跑（可按需改成先 shutdown 再建）

    m_logThread = new QThread(this);
    m_logWorker = new CsvLogWorker(&m_logBuffer);
    m_logWorker->setOutputPath(m_csvOutputPath);
    m_logWorker->setFlushIntervalMs(100);
    m_logWorker->setBatchSize(1024);
    m_logWorker->moveToThread(m_logThread);

    QObject::connect(m_logThread, &QThread::started, m_logWorker, &CsvLogWorker::start);
    QObject::connect(m_logThread, &QThread::finished, m_logWorker, &QObject::deleteLater);
    QObject::connect(m_logWorker, &CsvLogWorker::workerError, this, [this](const QString &msg) {
        emit testMessage(QStringLiteral("CSV: %1").arg(msg));
    });

    m_logThread->start();
}
```

### 2.2 `shutdownLogWriter()` 示例（从原 `stop()` 里拆出日志部分）

```cpp
void ThinkGearLinkTester::shutdownLogWriter()
{
    if (m_logWorker) {
        QMetaObject::invokeMethod(m_logWorker, "stop", Qt::QueuedConnection);
    }
    if (m_logThread) {
        m_logThread->quit();
        m_logThread->wait(2000);
        m_logThread->deleteLater();
        m_logThread = nullptr;
        m_logWorker = nullptr;
    }
}
```

### 2.3 `start()` 内应类似这样（**删除**原来的 `m_logThread->start();` + `stop();` 整块，改为）

```cpp
bool ThinkGearLinkTester::start(const QString &portName, qint32 baudRate)
{
    // 仅关串口，不要 stop() 整包
    if (m_serial && m_serial->isOpen())
        m_serial->close();

    shutdownLogWriter(); // 先停旧写盘（若存在）

    m_pictureandalgBuffer.clear();
    m_pictureandalgBuffer.resetAlgoCursor();
    m_pictureandalgBuffer.resetPlotCursor();
    m_seq = 0;

    if (m_csvLoggingEnabled)
        startupLogWriter();

    SerialPortConfig cfg;
    cfg.portName = portName;
    cfg.baudRate = baudRate;
    cfg.readBufferSize = 32 * 1024;
    m_serial->setConfig(cfg);

    const bool ok = m_serial->openReadOnly();
    if (!ok) {
        emit testMessage(QStringLiteral("串口打开失败"));
        shutdownLogWriter();
        return false;
    }

    m_rawCountSec = 0;
    m_frameCountSec = 0;
    m_warningCountSec = 0;
    m_secIndex = 0;
    m_tick.start();
    emit testMessage(QStringLiteral("测试开始：等待每秒统计..."));
    return true;
}
```

### 2.4 `stop()` 示例

```cpp
void ThinkGearLinkTester::stop()
{
    if (m_tick.isActive())
        m_tick.stop();
    if (m_serial && m_serial->isOpen())
        m_serial->close();
    shutdownLogWriter();
}
```

### 2.5 构造函数里**不要**再提前创建 `m_logThread`

你原来在第一次 `start()` 里 `if (!m_logThread) { ... }` 创建线程；按上面改完后，**统一在 `startupLogWriter()` 创建**，构造函数保持只连串口/解析即可。

---

## 三、`onRawUvReady` 可选：给 CSV 写上 `rawInt16`

`LogItem` 有 `rawInt16`，你当前只写了 `rawUv`。若希望 CSV 里 `rawInt16` 列有意义，需在 `RawtOutUvProcessor` 侧带原始值，或暂时填 0。  
最小改法（仅示意，需你有 `qint16 raw` 来源）：

```cpp
// li.rawInt16 = rawFromParser;
```

若暂时没有，可保持现状（列为 0）。

---

## 四、`MainWindow::mainwindow.h` 建议统一命名

把 `m_csvLoggingEnable` 改为 **`m_csvLoggingEnabled`**（与文档一致，避免拼写歧义）。  
若保留旧名，下文代码里自行替换。

```cpp
    bool m_csvLoggingEnabled = false;
```

若主窗口**没有** `label_logCsvPath` / `pushButton_openLogCsv`，可删掉声明里的 `on_pushButton_openLogCsv_clicked`，或按 `UI补充_CSV路径与勾选框_操作说明.md` 在 `.ui` 里补上。

---

## 五、`MainWindow::makeDefaultCsvPath()` 正确写法

```cpp
QString MainWindow::makeDefaultCsvPath()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString sub = QStringLiteral("QtBCI_logs");
    const QString dirPath = root + QLatin1Char('/') + sub;
    QDir().mkpath(dirPath);

    const QString name = QStringLiteral("raw_%1.csv")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss")));
    return QFileInfo(dirPath, name).absoluteFilePath();
}
```

---

## 六、`attachLinkTester` 与 `updateLogCsvPathUi`（若你界面无标签可简化）

**`mainwindow.cpp`** 在构造里补上（若尚未写）：

```cpp
void MainWindow::attachLinkTester(ThinkGearLinkTester *tester)
{
    m_tester = tester;
    setupTesterConnections();
}
```

**`updateLogCsvPathUi()`**：若 `.ui` 没有 `label_logCsvPath`，可改成只写状态栏：

```cpp
void MainWindow::updateLogCsvPathUi()
{
    if (m_currentCsvPath.isEmpty())
        ui->statusbar->showMessage(QStringLiteral("CSV：本次未启用"), 3000);
    else
        ui->statusbar->showMessage(QStringLiteral("CSV：%1").arg(m_currentCsvPath), 5000);
}
```

若有 `ui->label_logCsvPath`，则用 `UI与ThinkGear业务层对接示例.md` 里的版本。

---

## 七、`on_pushButton_start_clicked()` 完整示例（接 `m_csvLoggingEnabled` + 路径）

```cpp
void MainWindow::on_pushButton_start_clicked()
{
    if (!m_tester) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("未绑定 ThinkGearLinkTester"));
        return;
    }

    if (m_csvLoggingEnabled) {
        m_currentCsvPath = makeDefaultCsvPath();
        m_tester->setCsvLoggingEnabled(true);
        m_tester->setCsvOutputPath(m_currentCsvPath);
    } else {
        m_currentCsvPath.clear();
        m_tester->setCsvLoggingEnabled(false);
        m_tester->setCsvOutputPath(QString());
    }
    updateLogCsvPathUi();

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
    if (started)
        ui->statusbar->showMessage(QStringLiteral("已打开 %1 @57600").arg(port.trimmed()), 5000);
}
```

---

## 八、`on_pushButton_save_clicked()`：弹出「CSV 设置」对话框（与 `UI保存按钮_CSV设置对话框示例.md` 一致）

在 `mainwindow.cpp` 顶部增加：

```cpp
#include <QDialog>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
```

实现：

```cpp
void MainWindow::on_pushButton_save_clicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("保存与 CSV 记录"));
    dlg.setMinimumWidth(420);

    auto *lay = new QVBoxLayout(&dlg);

    auto *cb = new QCheckBox(QStringLiteral("开始采集时将 CSV 保存到默认目录"), &dlg);
    cb->setChecked(m_csvLoggingEnabled);

    auto *pathPreview = new QLabel(&dlg);
    pathPreview->setWordWrap(true);
    pathPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);

    const auto refreshPreview = [&] {
        if (cb->isChecked())
            pathPreview->setText(
                QStringLiteral("下次点击「开始」时，将使用类似路径：\n%1").arg(makeDefaultCsvPath()));
        else
            pathPreview->setText(QStringLiteral("下次「开始」不写入 CSV。"));
    };
    QObject::connect(cb, &QCheckBox::toggled, &dlg, [refreshPreview](bool) { refreshPreview(); });
    refreshPreview();

    auto *btnOpenFolder = new QPushButton(QStringLiteral("打开默认保存文件夹"), &dlg);
    QObject::connect(btnOpenFolder, &QPushButton::clicked, &dlg, [] {
        const QString root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString dirPath = root + QLatin1Char('/') + QStringLiteral("QtBCI_logs");
        QDir().mkpath(dirPath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
    });

    auto *btnLocateCsv = new QPushButton(QStringLiteral("在资源管理器中定位当前 CSV"), &dlg);
    QObject::connect(btnLocateCsv, &QPushButton::clicked, this, [&dlg, this] {
        if (m_currentCsvPath.isEmpty()) {
            QMessageBox::information(&dlg, QStringLiteral("提示"),
                                     QStringLiteral("当前没有 CSV 路径（未启用或未开始）。"));
            return;
        }
        if (!QFile::exists(m_currentCsvPath)) {
            QMessageBox::information(&dlg, QStringLiteral("提示"),
                                     QStringLiteral("文件尚未生成：\n%1").arg(m_currentCsvPath));
            return;
        }
#if defined(Q_OS_WIN)
        const QString native = QDir::toNativeSeparators(m_currentCsvPath);
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), native});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_currentCsvPath).absolutePath()));
#endif
    });

    auto *btnExportTxt = new QPushButton(QStringLiteral("导出界面日志为 TXT…"), &dlg);
    QObject::connect(btnExportTxt, &QPushButton::clicked, this, [this, &dlg] {
        const QString path = QFileDialog::getSaveFileName(
            &dlg,
            QStringLiteral("导出"),
            QStringLiteral("report_%1.txt")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"))),
            QStringLiteral("文本 (*.txt);;所有文件 (*.*)"));
        if (path.isEmpty())
            return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(&dlg, QStringLiteral("错误"), f.errorString());
            return;
        }
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        for (int i = 0; i < ui->listWidget_text->count(); ++i)
            out << ui->listWidget_text->item(i)->text() << '\n';
    });

    lay->addWidget(cb);
    lay->addWidget(pathPreview);
    lay->addWidget(btnOpenFolder);
    lay->addWidget(btnLocateCsv);
    lay->addWidget(btnExportTxt);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return;

    m_csvLoggingEnabled = cb->isChecked();
    updateLogCsvPathUi();
}
```

> 若你头文件仍用 `m_csvLoggingEnable`，把上面最后一行赋值改成对该成员赋值即可。

---

## 九、`onSecondReport` / `onTestMessage`（你当前 `mainwindow.cpp` 缺失，需补上）

```cpp
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

构造里记得调用 `updateLogCsvPathUi()` 一次（可选）。

---

## 十、`CsvLogWorker`：你现有实现可保持不变

`core/csvlogworker.cpp` / `.h` **无需为对接再改**；仅建议把表头里的 `tsMS` 改成 `tsMs`（与 `LogItem::tsMs` 一致），属于美观问题：

```cpp
m_out << "tsMs,seq,rawInt16,rawUv\n";
```

---

## 十一、`main.cpp` 确认

应类似：

```cpp
QApplication app(argc, argv);
MainWindow w;
auto *tester = new ThinkGearLinkTester(&w);
w.attachLinkTester(tester);
w.show();
return app.exec();
```

---

## 十二、验收清单

1. 点「保存」→ 对话框勾选 CSV → 确定 → 再「开始」→ `Documents/QtBCI_logs` 下出现 `raw_时间戳.csv`，且 `logQ` 不再顶满 4096。  
2. 不勾选 CSV → 「开始」→ 无 CSV 文件（或不再启动 worker），`logQ` 仍会涨（若仍 `push`）；若希望不勾选时也不 `push`，需在 `onRawUvReady` 里加 `if (m_csvLoggingEnabled)` 包住 `m_logBuffer.push`（可选扩展）。  
3. 「停止」后线程退出，可再次「开始」生成新 CSV。

---

*文档生成依据：仓库内 `thinkgearlinktester.cpp` 第 58–105 行、`mainwindow.cpp` 当前片段、`mainwindow.h` 成员命名；若你后续又改了类接口，请以本文结构为模板自行对齐。*
