TARGET = tst_qquickpainteditem
CONFIG += testcase
macx:CONFIG -= app_bundle

SOURCES += tst_qquickpainteditem.cpp

CONFIG += parallel_test

QT += core-private gui-private qml-private quick-private v8-private network testlib