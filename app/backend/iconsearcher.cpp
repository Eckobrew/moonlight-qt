#include "iconsearcher.h"

#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkCookieJar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QDebug>

IconSearcher::IconSearcher(QObject* parent) :
    QObject(parent),
    m_Nam(new QNetworkAccessManager(this)),
    m_CurrentSearchReply(nullptr)
{
    m_Nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    m_Nam->setCookieJar(new QNetworkCookieJar(this));
}

IconSearcher::~IconSearcher()
{
    if (m_CurrentSearchReply) {
        m_CurrentSearchReply->abort();
        m_CurrentSearchReply->deleteLater();
        m_CurrentSearchReply = nullptr;
    }
}

void IconSearcher::search(const QString& query, const QString& engine)
{
    if (m_CurrentSearchReply) {
        m_CurrentSearchReply->abort();
        m_CurrentSearchReply->deleteLater();
        m_CurrentSearchReply = nullptr;
    }

    if (engine.compare("DuckDuckGo", Qt::CaseInsensitive) == 0) {
        searchDuckDuckGo(query);
    } else {
        searchGoogle(query);
    }
}

void IconSearcher::searchDuckDuckGo(const QString& query)
{
    QUrl url(QString("https://duckduckgo.com/?q=%1&iax=images&ia=images")
                 .arg(QString::fromUtf8(QUrl::toPercentEncoding(query))));
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.5");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_Nam->get(request);
    m_CurrentSearchReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, query]() {
        if (reply != m_CurrentSearchReply) {
            reply->deleteLater();
            return;
        }
        m_CurrentSearchReply = nullptr;
        handleDuckDuckGoStep1(reply, query);
        reply->deleteLater();
    });
}

void IconSearcher::handleDuckDuckGoStep1(QNetworkReply* reply, const QString& query)
{
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            emit searchFinished(QVariantList(), reply->errorString());
        }
        return;
    }

    QString html = QString::fromUtf8(reply->readAll());
    static QRegularExpression reVqd("vqd=['\"]?([\\d-]+)['\"]?");
    QRegularExpressionMatch match = reVqd.match(html);
    if (!match.hasMatch()) {
        emit searchFinished(QVariantList(), tr("Failed to get search token from DuckDuckGo"));
        return;
    }

    QString vqd = match.captured(1);
    QUrl apiUrl(QString("https://duckduckgo.com/i.js?l=us-en&o=json&q=%1&vqd=%2&f=,,,&p=1")
                    .arg(QString::fromUtf8(QUrl::toPercentEncoding(query)), vqd));
    QNetworkRequest request(apiUrl);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "application/json, text/javascript, */*; q=0.01");
    request.setRawHeader("Referer", "https://duckduckgo.com/");
    request.setRawHeader("X-Requested-With", "XMLHttpRequest");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply2 = m_Nam->get(request);
    m_CurrentSearchReply = reply2;

    connect(reply2, &QNetworkReply::finished, this, [this, reply2]() {
        if (reply2 != m_CurrentSearchReply) {
            reply2->deleteLater();
            return;
        }
        m_CurrentSearchReply = nullptr;
        handleDuckDuckGoStep2(reply2);
        reply2->deleteLater();
    });
}

void IconSearcher::handleDuckDuckGoStep2(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            emit searchFinished(QVariantList(), reply->errorString());
        }
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QVariantList results;
    QSet<QString> seen;

    if (doc.isObject()) {
        QJsonArray arr = doc.object().value("results").toArray();
        for (const QJsonValue& val : arr) {
            if (val.isObject()) {
                QJsonObject itemObj = val.toObject();
                QString img = itemObj.value("image").toString();
                QString thumb = itemObj.value("thumbnail").toString();
                QString title = itemObj.value("title").toString();
                if (!img.isEmpty() && !seen.contains(img)) {
                    seen.insert(img);
                    QVariantMap m;
                    m["fullUrl"] = img;
                    m["thumbUrl"] = thumb.isEmpty() ? img : thumb;
                    m["title"] = title;
                    results.append(m);
                }
            }
        }
    }

    emit searchFinished(results, QString());
}

void IconSearcher::searchGoogle(const QString& query)
{
    QUrl url(QString("https://www.google.com/search?q=%1&tbm=isch&gl=us&hl=en")
                 .arg(QString::fromUtf8(QUrl::toPercentEncoding(query))));
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    request.setRawHeader("Cookie", "SOCS=CAISHAgBEhJnd3NfMjAyMzA4MTAtMF9SQzIaAmVuIAEaBgiAo_CmBg");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_Nam->get(request);
    m_CurrentSearchReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, query]() {
        if (reply != m_CurrentSearchReply) {
            reply->deleteLater();
            return;
        }
        m_CurrentSearchReply = nullptr;
        handleGoogleReply(reply, query);
        reply->deleteLater();
    });
}

void IconSearcher::handleGoogleReply(QNetworkReply* reply, const QString& query)
{
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            searchBing(query);
        }
        return;
    }

    QString html = QString::fromUtf8(reply->readAll());
    QVariantList results;
    QSet<QString> seenUrls;

    static QRegularExpression reImg("\\[\"(https?://[^\"<>\\s\\\\]+\\.(?:png|ico|jpg|jpeg|webp))\"");
    QRegularExpressionMatchIterator itImg = reImg.globalMatch(html);
    while (itImg.hasNext()) {
        QString u = itImg.next().captured(1);
        if (!u.contains("gstatic.com") && !seenUrls.contains(u)) {
            seenUrls.insert(u);
            QVariantMap item;
            item["fullUrl"] = u;
            item["thumbUrl"] = u;
            item["title"] = QString();
            results.append(item);
        }
    }

    static QRegularExpression reTbn("(https://encrypted-tbn\\d\\.gstatic\\.com/images\\?q=[^\"&\\\\'\\s]+)");
    QRegularExpressionMatchIterator itTbn = reTbn.globalMatch(html);
    int tbnIndex = 0;
    while (itTbn.hasNext()) {
        QString tbn = itTbn.next().captured(1);
        if (tbnIndex < results.size()) {
            QVariantMap item = results[tbnIndex].toMap();
            item["thumbUrl"] = tbn;
            results[tbnIndex] = item;
        } else if (!seenUrls.contains(tbn)) {
            seenUrls.insert(tbn);
            QVariantMap item;
            item["fullUrl"] = tbn;
            item["thumbUrl"] = tbn;
            item["title"] = QString();
            results.append(item);
        }
        tbnIndex++;
    }

    // If Google returned 0 results (due to Google anti-bot JavaScript challenge),
    // fallback gracefully to web results so the user always receives good icon results
    if (results.isEmpty()) {
        searchBing(query);
        return;
    }

    emit searchFinished(results, QString());
}

void IconSearcher::searchBing(const QString& query)
{
    QUrl url(QString("https://www.bing.com/images/search?q=%1&FORM=HDRSC2")
                 .arg(QString::fromUtf8(QUrl::toPercentEncoding(query))));
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.5");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_Nam->get(request);
    m_CurrentSearchReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply != m_CurrentSearchReply) {
            reply->deleteLater();
            return;
        }
        m_CurrentSearchReply = nullptr;
        handleBingReply(reply);
        reply->deleteLater();
    });
}

void IconSearcher::handleBingReply(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            emit searchFinished(QVariantList(), reply->errorString());
        }
        return;
    }

    QString html = QString::fromUtf8(reply->readAll());
    QVariantList results;
    QSet<QString> seenUrls;

    static QRegularExpression reIusc("class=\"iusc\"[^>]*?m=\"([^\"]+)\"");
    QRegularExpressionMatchIterator it = reIusc.globalMatch(html);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString rawJson = match.captured(1);
        rawJson.replace("&quot;", "\"");
        rawJson.replace("&amp;", "&");
        rawJson.replace("&lt;", "<");
        rawJson.replace("&gt;", ">");

        QJsonDocument doc = QJsonDocument::fromJson(rawJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            QString murl = obj.value("murl").toString();
            QString turl = obj.value("turl").toString();
            QString title = obj.value("t").toString();
            if (title.isEmpty()) {
                title = obj.value("desc").toString();
            }
            if (!murl.isEmpty() && !seenUrls.contains(murl)) {
                seenUrls.insert(murl);
                QVariantMap item;
                item["fullUrl"] = murl;
                item["thumbUrl"] = turl.isEmpty() ? murl : turl;
                item["title"] = title;
                results.append(item);
            }
        }
    }

    if (results.isEmpty()) {
        static QRegularExpression reMurl("murl&quot;:&quot;(https?://[^&]+)&quot;");
        static QRegularExpression reTurl("turl&quot;:&quot;(https?://[^&]+)&quot;");
        QRegularExpressionMatchIterator itM = reMurl.globalMatch(html);
        QRegularExpressionMatchIterator itT = reTurl.globalMatch(html);
        while (itM.hasNext()) {
            QString murl = itM.next().captured(1);
            QString turl = itT.hasNext() ? itT.next().captured(1) : murl;
            if (!murl.isEmpty() && !seenUrls.contains(murl)) {
                seenUrls.insert(murl);
                QVariantMap item;
                item["fullUrl"] = murl;
                item["thumbUrl"] = turl;
                item["title"] = QString();
                results.append(item);
            }
        }
    }

    emit searchFinished(results, QString());
}

void IconSearcher::downloadImage(const QString& fullUrl, const QString& thumbUrl, const QString& targetPath)
{
    QUrl url(fullUrl);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "image/png,image/svg+xml,image/*;q=0.8,*/*;q=0.5");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_Nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, thumbUrl, targetPath]() {
        bool success = false;
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QImage img;
            if (img.loadFromData(data) && !img.isNull()) {
                QFileInfo fi(targetPath);
                fi.dir().mkpath(".");
                if (img.save(targetPath, "PNG")) {
                    success = true;
                }
            }
        }
        reply->deleteLater();

        if (success) {
            emit downloadFinished(true, targetPath, QString());
        } else if (!thumbUrl.isEmpty()) {
            downloadFallbackThumb(thumbUrl, targetPath);
        } else {
            emit downloadFinished(false, QString(), tr("Failed to download selected icon image"));
        }
    });
}

void IconSearcher::downloadFallbackThumb(const QString& thumbUrl, const QString& targetPath)
{
    QUrl url(thumbUrl);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_Nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, targetPath]() {
        bool success = false;
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QImage img;
            if (img.loadFromData(data) && !img.isNull()) {
                QFileInfo fi(targetPath);
                fi.dir().mkpath(".");
                if (img.save(targetPath, "PNG")) {
                    success = true;
                }
            }
        }
        reply->deleteLater();

        if (success) {
            emit downloadFinished(true, targetPath, QString());
        } else {
            emit downloadFinished(false, QString(), tr("Failed to download selected icon image"));
        }
    });
}
