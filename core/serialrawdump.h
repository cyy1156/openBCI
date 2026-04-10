#ifndef SERIALRAWDUMP_H
#define SERIALRAWDUMP_H

#include <QByteArray>
#include <QString>

class QSerialPort;

/**
 * 串口原始字节 → 十六进制字符串（调试用）。
 * 高频数据流请勿每条都打印，会拖垮 UI / 丢包。
 */
namespace SerialRawDump {

/** 空格分隔十六进制，例如 "AA BB 0C" */
QString toHexSpaced(const QByteArray &ba);

/** 连续小写十六进制，例如 "aabb0c" */
QString toHexCompact(const QByteArray &ba);

/** 连续大写十六写进制，例如 "AABB0C" */
QString toHexCompactUpper(const QByteArray &ba);

/**
 * 多行 hexdump：偏移 + 十六进制列 + ASCII 列（类似 xxd）。
 * @param bytesPerLine 每行字节数，默认 16
 */
QString hexDumpLines(const QByteArray &ba, int bytesPerLine = 16);

/** 只取前 maxBytes 字节再转 hex，避免一次日志过长 */
QString toHexSpacedHead(const QByteArray &ba, int maxBytes = 64);

/**
 * 在 QSerialPort::readyRead 里读取并可选打印（演示用）。
 * @param port 已打开的串口
 * @param rxBuffer 累积缓冲（传出追加后的内容）
 * @param echoHex 若为 true，用 qDebug 打印本次读到的十六进制块
 * @return 本次 readAll() 读到的字节数
 */
qint64 readAllAppend(QSerialPort *port, QByteArray &rxBuffer, bool echoHex = false);

} // namespace SerialRawDump

#endif
