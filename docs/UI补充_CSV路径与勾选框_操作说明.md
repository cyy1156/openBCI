# 解决「`ui` 里没有 `label_logCsvPath` 等变量」— 操作说明

`UI与ThinkGear业务层对接示例.md` 里的示例代码使用了 **`ui->label_logCsvPath`**、**`ui->checkBox_logCsv`**、**`ui->pushButton_openLogCsv`**。  
这些不是 Qt 自带的隐藏成员，而是必须在 **`mainwindow.ui`** 里**放置控件并把 objectName 设成这几个名字**之后，由 `uic` 生成到 `ui_mainwindow.h` 里，才会有 `ui->label_logCsvPath` 这类指针。

下面给你两种方案，任选其一。

---

## 方案一（推荐）：在 Qt Designer 里加到 `mainwindow.ui`

### 1. 打开界面文件

用 Qt Creator 打开工程里的 `mainwindow.ui`，进入**设计**模式。

### 2. 拖三个控件到合适布局里

| 控件类型 | 放置建议 | **必须设置的 objectName**（右键 → 改变 objectName） |
|----------|----------|------------------------------------------------------|
| `QCheckBox` | 控制面板一列，例如「开始」按钮上方 | `checkBox_logCsv` |
| `QLabel` | 勾选框下方或侧栏，用于显示长路径 | `label_logCsvPath` |
| `QPushButton` | 「打开 CSV」快捷方式 | `pushButton_openLogCsv` |

可选属性：

- `checkBox_logCsv`：文案例如「保存 CSV 到默认目录」。
- `label_logCsvPath`：勾选 **wordWrap**，适当设置 `minimumHeight`（如 40），避免路径一行挤爆布局。
- `pushButton_openLogCsv`：文案例如「打开 CSV 所在位置」。

### 3. 保存并重新编译

保存 `.ui` 后执行一次 **构建**。  
CMake/Qt 会重新生成 `ui_mainwindow.h`，其中会出现：

```cpp
QLabel *label_logCsvPath;
QCheckBox *checkBox_logCsv;
QPushButton *pushButton_openLogCsv;
```

此时示例里的 `ui->label_logCsvPath` 等才能编译通过。

### 4. 「转到槽」连接 `pushButton_openLogCsv`

在 Designer 里对 `pushButton_openLogCsv` 右键 → **转到槽** → 选 `clicked()`，会生成：

```cpp
void MainWindow::on_pushButton_openLogCsv_clicked();
```

把文档示例里该槽函数的实现复制进去即可（若已手写同名槽，不要重复生成）。

---

## 方案二：不改 `.ui`，在 `MainWindow` 构造函数里用代码创建

若你暂时不想动 Designer，可以在 `MainWindow` 构造里**自己 `new` 控件并设 objectName**，再挂到现有布局上。这样**没有** `ui->label_logCsvPath`，要把示例里的 `ui->label_logCsvPath` 改成**成员指针**（例如 `m_labelLogCsvPath`）。

### 思路概要

1. 在 `mainwindow.h` 增加成员：

```cpp
private:
    QCheckBox *m_checkBoxLogCsv = nullptr;
    QLabel *m_labelLogCsvPath = nullptr;
    QPushButton *m_btnOpenLogCsv = nullptr;
```

2. 在 `MainWindow` 构造函数里 `setupUi` 之后：

```cpp
m_checkBoxLogCsv = new QCheckBox(QStringLiteral("保存 CSV 到默认目录"), this);
m_labelLogCsvPath = new QLabel(QStringLiteral("当前 CSV：未启用"), this);
m_labelLogCsvPath->setWordWrap(true);
m_btnOpenLogCsv = new QPushButton(QStringLiteral("打开 CSV…"), this);

// 把三者放进你已有的 QVBoxLayout，例如：
// ui->verticalLayout_3->addWidget(m_checkBoxLogCsv);
// ui->verticalLayout_3->addWidget(m_labelLogCsvPath);
// ui->verticalLayout_3->addWidget(m_btnOpenLogCsv);
```

3. 把文档示例中所有：

- `ui->checkBox_logCsv` → `m_checkBoxLogCsv`
- `ui->label_logCsvPath` → `m_labelLogCsvPath`
- `ui->pushButton_openLogCsv` → `m_btnOpenLogCsv`

4. `connect(m_btnOpenLogCsv, &QPushButton::clicked, this, &MainWindow::on_pushButton_openLogCsv_clicked);`  
   （若不用转到槽，需手写这一行。）

---

## 为什么会出现 `if (!ui->label_logCsvPath)`？

文档里加这一判断是为了：**万一某次复制代码时还没加控件**，避免空指针崩溃。  
**正常情况**下，只要你按方案一在 `.ui` 里加了 `label_logCsvPath`，`ui->label_logCsvPath` 在 `setupUi` 之后**永远非空**，这个 `if` 可以删掉或保留均可。

---

## 自检清单

- [ ] `.ui` 里三个 **objectName** 与代码里用到的名字**完全一致**（区分大小写）。  
- [ ] 保存 `.ui` 后已**重新编译**，`ui_mainwindow.h` 已更新。  
- [ ] `on_pushButton_openLogCsv_clicked` 已实现并与按钮连接。

---

*本文仅说明如何补齐 UI；业务层 `setCsvLoggingEnabled` / `setCsvOutputPath` 仍见 `UI与ThinkGear业务层对接示例.md` 第六节。*
