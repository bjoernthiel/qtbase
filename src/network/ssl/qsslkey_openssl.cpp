/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtNetwork module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/


#include "qsslkey.h"
#include "qsslkey_p.h"
#include "qsslsocket_openssl_symbols_p.h"
#include "qsslsocket.h"
#include "qsslsocket_p.h"

#include <QtCore/qatomic.h>
#include <QtCore/qbytearray.h>
#include <QtCore/qiodevice.h>
#ifndef QT_NO_DEBUG_STREAM
#include <QtCore/qdebug.h>
#endif

QT_BEGIN_NAMESPACE

void QSslKeyPrivate::clear(bool deep)
{
    isNull = true;
    if (!QSslSocket::supportsSsl())
        return;
    if (rsa) {
        if (deep)
            q_RSA_free(rsa);
        rsa = 0;
    }
    if (dsa) {
        if (deep)
            q_DSA_free(dsa);
        dsa = 0;
    }
#ifndef OPENSSL_NO_EC
    if (ec) {
       if (deep)
            q_EC_KEY_free(ec);
       ec = 0;
    }
#endif
    if (opaque) {
        if (deep)
            q_EVP_PKEY_free(opaque);
        opaque = 0;
    }
}

bool QSslKeyPrivate::fromEVP_PKEY(EVP_PKEY *pkey)
{
    if (pkey->type == EVP_PKEY_RSA) {
        isNull = false;
        algorithm = QSsl::Rsa;
        type = QSsl::PrivateKey;

        rsa = q_RSA_new();
        memcpy(rsa, q_EVP_PKEY_get1_RSA(pkey), sizeof(RSA));

        return true;
    }
    else if (pkey->type == EVP_PKEY_DSA) {
        isNull = false;
        algorithm = QSsl::Dsa;
        type = QSsl::PrivateKey;

        dsa = q_DSA_new();
        memcpy(dsa, q_EVP_PKEY_get1_DSA(pkey), sizeof(DSA));

        return true;
    }
#ifndef OPENSSL_NO_EC
    else if (pkey->type == EVP_PKEY_EC) {
        isNull = false;
        algorithm = QSsl::Ec;
        type = QSsl::PrivateKey;
        ec = q_EC_KEY_dup(q_EVP_PKEY_get1_EC_KEY(pkey));

        return true;
    }
#endif
    else {
        // Unknown key type. This could be handled as opaque, but then
        // we'd eventually leak memory since we wouldn't be able to free
        // the underlying EVP_PKEY structure. For now, we won't support
        // this.
    }

    return false;
}

void QSslKeyPrivate::decodeDer(const QByteArray &der, bool deepClear)
{
    QMap<QByteArray, QByteArray> headers;
    decodePem(pemFromDer(der, headers), QByteArray(), deepClear);
}

void QSslKeyPrivate::decodePem(const QByteArray &pem, const QByteArray &passPhrase,
                               bool deepClear)
{
    if (pem.isEmpty())
        return;

    clear(deepClear);

    if (!QSslSocket::supportsSsl())
        return;

    BIO *bio = q_BIO_new_mem_buf(const_cast<char *>(pem.data()), pem.size());
    if (!bio)
        return;

    void *phrase = (void *)passPhrase.constData();

    if (algorithm == QSsl::Rsa) {
        RSA *result = (type == QSsl::PublicKey)
            ? q_PEM_read_bio_RSA_PUBKEY(bio, &rsa, 0, phrase)
            : q_PEM_read_bio_RSAPrivateKey(bio, &rsa, 0, phrase);
        if (rsa && rsa == result)
            isNull = false;
    } else if (algorithm == QSsl::Dsa) {
        DSA *result = (type == QSsl::PublicKey)
            ? q_PEM_read_bio_DSA_PUBKEY(bio, &dsa, 0, phrase)
            : q_PEM_read_bio_DSAPrivateKey(bio, &dsa, 0, phrase);
        if (dsa && dsa == result)
            isNull = false;
#ifndef OPENSSL_NO_EC
    } else if (algorithm == QSsl::Ec) {
        EC_KEY *result = (type == QSsl::PublicKey)
            ? q_PEM_read_bio_EC_PUBKEY(bio, &ec, 0, phrase)
            : q_PEM_read_bio_ECPrivateKey(bio, &ec, 0, phrase);
        if (ec && ec == result)
            isNull = false;
#endif
    }

    q_BIO_free(bio);
}

int QSslKeyPrivate::length() const
{
    if (isNull || algorithm == QSsl::Opaque)
        return -1;

    switch (algorithm) {
        case QSsl::Rsa: return q_BN_num_bits(rsa->n);
        case QSsl::Dsa: return q_BN_num_bits(dsa->p);
#ifndef OPENSSL_NO_EC
        case QSsl::Ec: return q_EC_GROUP_get_degree(q_EC_KEY_get0_group(ec));
#endif
        default: return -1;
    }
}

QByteArray QSslKeyPrivate::toPem(const QByteArray &passPhrase) const
{
    if (!QSslSocket::supportsSsl() || isNull || algorithm == QSsl::Opaque)
        return QByteArray();

    BIO *bio = q_BIO_new(q_BIO_s_mem());
    if (!bio)
        return QByteArray();

    bool fail = false;

    if (algorithm == QSsl::Rsa) {
        if (type == QSsl::PublicKey) {
            if (!q_PEM_write_bio_RSA_PUBKEY(bio, rsa))
                fail = true;
        } else {
            if (!q_PEM_write_bio_RSAPrivateKey(
                    bio, rsa,
                    // ### the cipher should be selectable in the API:
                    passPhrase.isEmpty() ? (const EVP_CIPHER *)0 : q_EVP_des_ede3_cbc(),
                    (uchar *)passPhrase.data(), passPhrase.size(), 0, 0)) {
                fail = true;
            }
        }
    } else if (algorithm == QSsl::Dsa) {
        if (type == QSsl::PublicKey) {
            if (!q_PEM_write_bio_DSA_PUBKEY(bio, dsa))
                fail = true;
        } else {
            if (!q_PEM_write_bio_DSAPrivateKey(
                    bio, dsa,
                    // ### the cipher should be selectable in the API:
                    passPhrase.isEmpty() ? (const EVP_CIPHER *)0 : q_EVP_des_ede3_cbc(),
                    (uchar *)passPhrase.data(), passPhrase.size(), 0, 0)) {
                fail = true;
            }
        }
#ifndef OPENSSL_NO_EC
    } else if (algorithm == QSsl::Ec) {
        if (type == QSsl::PublicKey) {
            if (!q_PEM_write_bio_EC_PUBKEY(bio, ec))
                fail = true;
        } else {
            if (!q_PEM_write_bio_ECPrivateKey(
                    bio, ec,
                    // ### the cipher should be selectable in the API:
                    passPhrase.isEmpty() ? (const EVP_CIPHER *)0 : q_EVP_des_ede3_cbc(),
                    (uchar *)passPhrase.data(), passPhrase.size(), 0, 0)) {
                fail = true;
            }
        }
#endif
    } else {
        fail = true;
    }

    QByteArray pem;
    if (!fail) {
        char *data;
        long size = q_BIO_get_mem_data(bio, &data);
        pem = QByteArray(data, size);
    }
    q_BIO_free(bio);
    return pem;
}

Qt::HANDLE QSslKeyPrivate::handle() const
{
    switch (algorithm) {
    case QSsl::Opaque:
        return Qt::HANDLE(opaque);
    case QSsl::Rsa:
        return Qt::HANDLE(rsa);
    case QSsl::Dsa:
        return Qt::HANDLE(dsa);
#ifndef OPENSSL_NO_EC
    case QSsl::Ec:
        return Qt::HANDLE(ec);
#endif
    default:
        return Qt::HANDLE(NULL);
    }
}

QT_END_NAMESPACE
