#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class IconSearcher : public QObject
{
    Q_OBJECT

public:
    explicit IconSearcher(QObject* parent = nullptr);
    ~IconSearcher() override;

    void search(const QString& query, const QString& engine = "Google");
    void downloadImage(const QString& fullUrl, const QString& thumbUrl, const QString& targetPath);

signals:
    void searchFinished(const QVariantList& results, const QString& error);
    void downloadFinished(bool success, const QString& localPath, const QString& error);

private:
    void searchGoogle(const QString& query);
    void searchDuckDuckGo(const QString& query);
    void searchBing(const QString& query);

    void handleGoogleReply(QNetworkReply* reply, const QString& query);
    void handleDuckDuckGoStep1(QNetworkReply* reply, const QString& query);
    void handleDuckDuckGoStep2(QNetworkReply* reply);
    void handleBingReply(QNetworkReply* reply);

    void downloadFallbackThumb(const QString& thumbUrl, const QString& targetPath);

    QNetworkAccessManager* m_Nam;
    QNetworkReply* m_CurrentSearchReply;
};
