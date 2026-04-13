# 「保存」按钮弹出对话框 — CSV 新建/路径显示/快捷打开（按你新需求）

> 本文仅提供示例代码，不直接修改你的工程源码。  
> 目标：点击 `pushButton_save` 后，弹出一个对话框，包含：
> - 是否启用 CSV 记录（勾选）
> - `新建 CSV` 功能（与 RealCtrl 风格一致：先生成新文件路径）
> - 显示当前 CSV 完整路径
> - 在对话框内一个“快捷打开当前 CSV”按钮（可直接定位/打开）
> - `OK/Cancel`（仅在 OK 时保存勾选状态）

---

## 一、MainWindow 里需要的成员（最小）

`mainwindow.h` 示例（已有可复用）：

```cpp
private:
    bool m_csvLoggingEnabled = false; // 保存对话框写入；开始采集时读取
    QString m_currentCsvPath;         // 当前 CSV 绝对路径（新建后写入）

    static QString makeDefaultCsvPath();
    void updateLogCsvPathUi();
```

---

## 二、默认路径函数（确保目录存在）

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

## 三、保存按钮槽：完整替换版

> 这个版本已经按你要求把“打开资源管理器”改成“显示当前 CSV 路径 + 新建 CSV + 快捷打开当前 CSV”。

```cpp
#include <QDialog>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>

void MainWindow::on_pushButton_save_clicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("保存与CSV记录"));
    dlg.setMinimumWidth(480);

    auto *lay = new QVBoxLayout(&dlg);

    // 1) 是否启用 CSV
    auto *cbEnable = new QCheckBox(QStringLiteral("开始采集时将 CSV 保存到默认目录"), &dlg);
    cbEnable->setChecked(m_csvLoggingEnabled);

    // 2) 当前 CSV 路径显示（替代原“资源管理器”按钮）
    auto *labelPath = new QLabel(&dlg);
    labelPath->setWordWrap(true);
    labelPath->setTextInteractionFlags(Qt::TextSelectableByMouse);

    const auto refreshPath = [&] {
        if (!cbEnable->isChecked()) {
            labelPath->setText(QStringLiteral("当前状态：未启用 CSV 记录"));
            return;
        }
        const QString showPath = m_currentCsvPath.isEmpty() ? makeDefaultCsvPath() : m_currentCsvPath;
        labelPath->setText(QStringLiteral("当前 CSV 路径：\n%1").arg(showPath));
    };
    QObject::connect(cbEnable, &QCheckBox::toggled, &dlg, [refreshPath](bool) { refreshPath(); });
    refreshPath();

    // 3) 新建 CSV（生成并保存到 m_currentCsvPath）
    auto *btnNewCsv = new QPushButton(QStringLiteral("新建 CSV"), &dlg);
    QObject::connect(btnNewCsv, &QPushButton::clicked, this, [this, &dlg, refreshPath] {
        m_currentCsvPath = makeDefaultCsvPath();
        refreshPath();
        QMessageBox::information(&dlg, QStringLiteral("新建成功"),
                                 QStringLiteral("已创建新的 CSV 路径：\n%1").arg(m_currentCsvPath));
    });

    // 4) 快捷打开当前 CSV（文件存在时定位/打开）
    auto *btnOpenCsv = new QPushButton(QStringLiteral("快捷打开当前 CSV"), &dlg);
    QObject::connect(btnOpenCsv, &QPushButton::clicked, this, [&dlg, this] {
        if (m_currentCsvPath.isEmpty()) {
            QMessageBox::information(&dlg, QStringLiteral("提示"),
                                     QStringLiteral("当前没有 CSV 路径，请先点击“新建 CSV”。"));
            return;
        }
        QFileInfo fi(m_currentCsvPath);
        if (!fi.exists()) {
            QMessageBox::information(&dlg, QStringLiteral("提示"),
                                     QStringLiteral("文件尚未生成（通常需要开始采集并写入后出现）：\n%1")
                                         .arg(m_currentCsvPath));
            return;
        }

#if defined(Q_OS_WIN)
        // Windows: 在资源管理器定位到文件
        const QString native = QDir::toNativeSeparators(m_currentCsvPath);
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), native});
#else
        // 其它系统: 用默认程序直接打开 CSV
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentCsvPath));
#endif
    });

    // 可选：保留导出 UI 文本日志
    auto *btnExportTxt = new QPushButton(QStringLiteral("导出界面日志为 TXT..."), &dlg);
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

    lay->addWidget(cbEnable);
    lay->addWidget(labelPath);
    lay->addWidget(btnNewCsv);
    lay->addWidget(btnOpenCsv);
    lay->addWidget(btnExportTxt);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return;

    // 仅在用户点 OK 时提交设置
    m_csvLoggingEnabled = cbEnable->isChecked();
    updateLogCsvPathUi();
}
```

---

## 四、和 `ThinkGearLinkTester` 的联动提醒

你在 `on_pushButton_start_clicked()` 里要按以下顺序处理：

1. 读取 `m_csvLoggingEnabled`
2. 若启用：
   - 如果 `m_currentCsvPath` 为空就 `m_currentCsvPath = makeDefaultCsvPath()`
   - `m_tester->setCsvLoggingEnabled(true);`
   - `m_tester->setCsvOutputPath(m_currentCsvPath);`
3. 若不启用：
   - `m_tester->setCsvLoggingEnabled(false);`

这样就实现了：
- 用户先在“保存”对话框里点“新建 CSV”
- 再点“开始”时按这个路径写盘

---

## 五、你现在界面上会有什么效果

- 点 `保存`：出现设置对话框
- 勾选框：控制下次开始是否写 CSV
- `新建 CSV`：立刻生成新路径并显示
- `快捷打开当前 CSV`：文件存在时可一键定位/打开
- 点 `OK`：保存本次设置；点 `Cancel`：不生效

# 「保存」按钮弹出对话框 — 集中处理 CSV 相关功能（示例）

> 本文**仅文档**：不直接改你工程文件。  
> 目标：点击界面上的 **`pushButton_save`** 时，弹出一个**模态对话框**，在里面完成：
> - 是否在本次「开始采集」时写入 CSV（勾选）  
> - 预览**默认路径**下将生成的文件名（文档目录 `/QtBCI_logs/raw_时间戳.csv`）  
> - **打开默认保存文件夹**（方便用户找历史 CSV）  
> - **定位当前已生成的 CSV**（若正在采集且文件已存在）  
> - **确定 / 取消**（确定后才把选项写回 `MainWindow` 的成员变量）

主窗口上可以**不再**放 `checkBox_logCsv` / `label_logCsvPath`（避免重复）；若你已按旧文档加过，可保留其一或删掉，以对话框为准。

---

## 一、`MainWindow` 里需要多两个成员（示例）

在 `mainwindow.h` 的 `private:` 里增加（名称可自定）：

```cpp
/** 由「保存」对话框写入；「开始」时根据它决定是否启 CSV */
bool m_csvLoggingEnabled = false;
```

已有的 **`m_currentCsvPath`** 仍可保留：在「开始」时若 `m_csvLoggingEnabled==true` 则 `makeDefaultCsvPath()` 赋给它并传给 `ThinkGearLinkTester`。

---

## 二、把「开始」里的勾选框改成读成员变量

把原先 `on_pushButton_start_clicked` 里：

```cpp
const bool wantCsv = ui->checkBox_logCsv && ui->checkBox_logCsv->isChecked();
```

改成：

```cpp
const bool wantCsv = m_csvLoggingEnabled;
```

其余逻辑（`setCsvLoggingEnabled` / `setCsvOutputPath` / `makeDefaultCsvPath`）与 `UI与ThinkGear业务层对接示例.md` 一致。

若主窗口还有 `label_logCsvPath`，可在对话框点「确定」后调用原来的 `updateLogCsvPathUi()`，或根据 `m_csvLoggingEnabled` 只更新文案（不勾选时显示「未启用 CSV」）。

---

## 三、`on_pushButton_save_clicked`：弹出对话框（完整示例）

下面可整体替换你文档里「导出 txt」的 `on_pushButton_save_clicked`，或把 txt 导出做成对话框里的第二个按钮（见第四节）。

```cpp
#include <QDialog>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#if defined(Q_OS_WIN)
#include <QtGlobal>
#endif

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
                QStringLiteral("下次点击「开始」时，将使用类似路径（时间戳以实际开始时刻为准）：\n%1")
                    .arg(makeDefaultCsvPath()));
        else
            pathPreview->setText(QStringLiteral("下次「开始」不写入 CSV 文件。"));
    };
    QObject::connect(cb, &QCheckBox::toggled, &dlg, [refreshPreview](bool) { refreshPreview(); });
    refreshPreview();

    auto *btnOpenFolder = new QPushButton(QStringLiteral("打开默认保存文件夹"), &dlg);
    QObject::connect(btnOpenFolder, &QPushButton::clicked, &dlg, [] {
        const QString root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString dirPath = root + QLatinChar('/') + QStringLiteral("QtBCI_logs");
        QDir().mkpath(dirPath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
    });

    auto *btnLocateCsv = new QPushButton(QStringLiteral("在资源管理器中定位当前 CSV"), &dlg);
    QObject::connect(btnLocateCsv, &QPushButton::clicked, this, [&dlg, this] {
        if (m_currentCsvPath.isEmpty()) {
            QMessageBox::information(&dlg, QStringLiteral("提示"),
                                     QStringLiteral("当前没有正在使用的 CSV 路径（可能未勾选保存或未开始采集）。"));
            return;
        }
        if (!QFile::exists(m_currentCsvPath)) {
            QMessageBox::information(&dlg, QStringLiteral("提示"),
                                     QStringLiteral("文件尚未生成或尚未首次写入：\n%1").arg(m_currentCsvPath));
            return;
        }
#if defined(Q_OS_WIN)
        const QString native = QDir::toNativeSeparators(m_currentCsvPath);
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), native});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_currentCsvPath).absolutePath()));
#endif
    });

    lay->addWidget(cb);
    lay->addWidget(pathPreview);
    lay->addWidget(btnOpenFolder);
    lay->addWidget(btnLocateCsv);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return;

    m_csvLoggingEnabled = cb->isChecked();
    updateLogCsvPathUi(); // 若你仍用主窗口标签显示状态；否则可改为 statusBar 一行提示
}
```

**说明**：

- 对话框里预览路径每次勾选变化会刷新；**真正路径**仍以**点击「开始」那一刻**的 `makeDefaultCsvPath()` 为准（避免对话框里预览和实际差几秒导致文件名不一致）。若你希望**预览即最终文件名**，可在点「确定」时把路径算好存到 `m_pendingCsvPath`，「开始」时优先用 `m_pendingCsvPath`——属于小扩展，此处不展开。  
- `makeDefaultCsvPath()`、`updateLogCsvPathUi()`、`m_currentCsvPath` 定义见 `UI与ThinkGear业务层对接示例.md`。

---

## 四、可选：同一对话框里保留「导出列表为 TXT」

若你仍需要把 `listWidget_text` 导出成报告，可在 `lay->addWidget(box)` 之前加一个按钮：

```cpp
auto *btnExportTxt = new QPushButton(QStringLiteral("导出界面日志为 TXT…"), &dlg);
QObject::connect(btnExportTxt, &QPushButton::clicked, this, [this] {
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出"),
        QStringLiteral("report_%1.txt").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"))),
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
});
lay->addWidget(btnExportTxt);
```

这样「保存」按钮 = **一个入口**：CSV 设置 + 可选 TXT 导出。

---

## 五、与 `ThinkGearLinkTester` 的衔接（提醒）

对话框只改 **`m_csvLoggingEnabled`**（和可选 UI 刷新）。  
业务层仍需实现（见主对接文档第六节）：

- `setCsvLoggingEnabled(bool)`  
- `setCsvOutputPath(const QString &)`  

并在 **`start()`** 里按标志决定是否启动 `CsvLogWorker`。

---

## 六、Designer 注意

- **`pushButton_save`** 保持 objectName 不变，继续用 `on_pushButton_save_clicked`。  
- 若已删除主界面上的 `checkBox_logCsv`，请同步删掉 `on_pushButton_start_clicked` 里对 `ui->checkBox_logCsv` 的引用，改为 **`m_csvLoggingEnabled`**。

---

*文档版本：与 `UI与ThinkGear业务层对接示例.md`、`UI补充_CSV路径与勾选框_操作说明.md` 可配合使用；主窗口是否保留路径标签由你决定。*

**按你仓库现状整理的「对话框 + `ThinkGearLinkTester` 线程 + `start/stop` 修正」全文见：`docs/完整对接_MainWindow与CSV_逐步代码.md`。**
