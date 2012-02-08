/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwindowsservices.h"
#include "qtwindows_additional.h"

#include <QtCore/QUrl>
#include <QtCore/QDebug>

#include <shlobj.h>
#include <intshcut.h>

QT_BEGIN_NAMESPACE

enum { debug = 0 };

static inline bool shellExecute(const QString &file)
{
    const int result = (int)ShellExecute(0, 0, (wchar_t*)file.utf16(), 0, 0, SW_SHOWNORMAL);
    // ShellExecute returns a value greater than 32 if successful
    if (result <= 32) {
        qWarning("ShellExecute '%s' failed (error %0x).", qPrintable(file), result);
        return false;
    }
    return true;
}

// Retrieve the commandline for the default mail client. It contains a
// placeholder %1 for the URL. The default key used below is the
// command line for the mailto: shell command.
static inline QString mailCommand()
{
    enum { BufferSize = sizeof(wchar_t) * MAX_PATH };

    const wchar_t mailUserKey[] = L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\mailto\\UserChoice";

    wchar_t command[MAX_PATH] = {0};
    // Check if user has set preference, otherwise use default.
    HKEY handle;
    QString keyName;
    if (!RegOpenKeyEx(HKEY_CURRENT_USER, mailUserKey, 0, KEY_READ, &handle)) {
        DWORD bufferSize = BufferSize;
        if (!RegQueryValueEx(handle, L"Progid", 0, 0, reinterpret_cast<unsigned char*>(command), &bufferSize))
            keyName = QString::fromWCharArray(command);
        RegCloseKey(handle);
    }
    if (keyName.isEmpty())
        keyName = QStringLiteral("mailto");
    keyName += QStringLiteral("\\Shell\\Open\\Command");
    if (debug)
        qDebug() << __FUNCTION__ << "keyName=" << keyName;
    command[0] = 0;
    if (!RegOpenKeyExW(HKEY_CLASSES_ROOT, (const wchar_t*)keyName.utf16(), 0, KEY_READ, &handle)) {
        DWORD bufferSize = BufferSize;
        RegQueryValueEx(handle, L"", 0, 0, reinterpret_cast<unsigned char*>(command), &bufferSize);
        RegCloseKey(handle);
    }
    if (!command[0])
        return QString();
    wchar_t expandedCommand[MAX_PATH] = {0};
    return ExpandEnvironmentStrings(command, expandedCommand, MAX_PATH) ?
           QString::fromWCharArray(expandedCommand) : QString::fromWCharArray(command);
}

static inline bool launchMail(const QUrl &url)
{
    QString command = mailCommand();
    if (command.isEmpty()) {
        qWarning("Cannot launch '%s': There is no mail program installed.");
        return false;
    }
    //Make sure the path for the process is in quotes
    const QChar doubleQuote = QLatin1Char('"');
    if (!command.startsWith(doubleQuote)) {
        const int exeIndex = command.indexOf(QStringLiteral(".exe "), 0, Qt::CaseInsensitive);
        if (exeIndex != -1) {
            command.insert(exeIndex + 4, doubleQuote);
            command.prepend(doubleQuote);
        }
    }
    // Pass the url as the parameter. Should use QProcess::startDetached(),
    // but that cannot handle a Windows command line [yet].
    command.replace(QStringLiteral("%1"), url.toString());
    if (debug)
        qDebug() << __FUNCTION__ << "Launching" << command;
    //start the process
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcess(NULL, (wchar_t*)command.utf16(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        qErrnoWarning("Unable to launch '%s'", qPrintable(command));
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

bool QWindowsServices::openUrl(const QUrl &url)
{
    const QString scheme = url.scheme();
    if (scheme.isEmpty())
        return openDocument(url);
    if (scheme == QStringLiteral("mailto") && launchMail(url))
        return true;
    return shellExecute(QLatin1String(url.toEncoded()));
}

bool QWindowsServices::openDocument(const QUrl &url)
{
    return shellExecute(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

QT_END_NAMESPACE
