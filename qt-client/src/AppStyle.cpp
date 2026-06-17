#include "AppStyle.h"

QString buildApplicationStyleSheet()
{
  return QStringLiteral(R"(
QWidget {
  background: #f5f7fb;
  color: #172033;
  font-family: "Segoe UI", "Inter", "Arial";
  font-size: 14px;
}

QMainWindow {
  background: #f5f7fb;
}

QWidget#loginWindow {
  background: #f5f7fb;
}

QWidget#sidebar {
  background: #eef3f8;
  border-right: 1px solid #dce4ef;
}

QWidget#chatPanel {
  background: #f8fafc;
}

QLabel#appTitle {
  color: #111827;
  font-size: 22px;
  font-weight: 700;
}

QLabel#chatTitle {
  color: #111827;
  font-size: 20px;
  font-weight: 700;
}

QLabel#sectionLabel {
  color: #64748b;
  font-size: 12px;
  font-weight: 700;
}

QLabel#userNameLabel {
  color: #111827;
  font-size: 17px;
  font-weight: 700;
}

QLabel#connectionStatus,
QLabel#actionStatus,
QLabel#loginStatus {
  border-radius: 10px;
  padding: 7px 10px;
}

QLabel#connectionStatus[status="connected"] {
  background: #e8f7ef;
  color: #146c43;
}

QLabel#connectionStatus[status="disconnected"] {
  background: #edf2f7;
  color: #64748b;
}

QLabel#connectionStatus[status="error"] {
  background: #fee2e2;
  color: #991b1b;
}

QLabel#actionStatus,
QLabel#loginStatus {
  background: #edf2f7;
  color: #475569;
}

QListWidget {
  background: #ffffff;
  border: 1px solid #dce4ef;
  border-radius: 12px;
  padding: 5px;
  outline: 0;
}

QListWidget::item {
  border-radius: 8px;
  color: #334155;
  margin: 2px;
  min-height: 28px;
  padding: 7px 9px;
}

QListWidget::item:hover {
  background: #e7eef8;
}

QListWidget::item:selected {
  background: #dbeafe;
  color: #1d4ed8;
}

QPushButton {
  background: #2563eb;
  border: 0;
  border-radius: 11px;
  color: #ffffff;
  font-weight: 700;
  min-height: 28px;
  padding: 8px 14px;
}

QPushButton:hover {
  background: #1d4ed8;
}

QPushButton:pressed {
  background: #1e40af;
}

QPushButton:disabled {
  background: #cbd5e1;
  color: #f8fafc;
}

QPushButton[variant="secondary"] {
  background: #ffffff;
  border: 1px solid #d5dfec;
  color: #334155;
}

QPushButton[variant="secondary"]:hover {
  background: #f1f5f9;
}

QPushButton[variant="secondary"]:pressed {
  background: #e2e8f0;
}

QLineEdit,
QTextEdit,
QSpinBox {
  background: #ffffff;
  border: 1px solid #d5dfec;
  border-radius: 11px;
  color: #172033;
  padding: 8px 10px;
  selection-background-color: #bfdbfe;
}

QLineEdit:focus,
QTextEdit:focus,
QSpinBox:focus {
  border: 1px solid #60a5fa;
}

QScrollArea#messageScrollArea {
  background: #ffffff;
  border: 1px solid #dce4ef;
  border-radius: 16px;
}

QWidget#messageContainer {
  background: #ffffff;
}

QWidget#messageRow {
  background: transparent;
}

QWidget#messageBubble {
  border-radius: 16px;
}

QWidget#messageBubble[kind="mine"] {
  background: #7c96cf;
}

QWidget#messageBubble[kind="other"] {
  background: #eef2f7;
}

QWidget#messageBubble[kind="system"] {
  background: #f1f5f9;
}

QWidget#messageBubble[kind="error"] {
  background: #fee2e2;
  border: 1px solid #fecaca;
}

QLabel#messageBubbleSender {
  font-size: 12px;
  font-weight: 700;
}

QLabel#messageBubbleText {
  font-size: 14px;
}

QLabel#messageBubbleTime {
  font-size: 11px;
}

QLabel#messageBubbleSender[bubbleKind="mine"],
QLabel#messageBubbleText[bubbleKind="mine"],
QLabel#messageBubbleTime[bubbleKind="mine"] {
  color: #000000;
}

QLabel#messageBubbleSender[bubbleKind="other"],
QLabel#messageBubbleText[bubbleKind="other"] {
  color: #172033;
}

QLabel#messageBubbleTime[bubbleKind="other"] {
  color: #64748b;
}

QLabel#messageBubbleSender[bubbleKind="system"],
QLabel#messageBubbleText[bubbleKind="system"],
QLabel#messageBubbleTime[bubbleKind="system"] {
  color: #64748b;
}

QLabel#messageBubbleSender[bubbleKind="error"],
QLabel#messageBubbleText[bubbleKind="error"],
QLabel#messageBubbleTime[bubbleKind="error"] {
  color: #991b1b;
}

QSplitter::handle {
  background: #dce4ef;
}

QScrollBar:vertical {
  background: transparent;
  border: 0;
  margin: 8px 3px 8px 3px;
  width: 10px;
}

QScrollBar::handle:vertical {
  background: #cbd5e1;
  border-radius: 5px;
  min-height: 28px;
}

QScrollBar::handle:vertical:hover {
  background: #94a3b8;
}

QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
  height: 0;
}
)");
}
