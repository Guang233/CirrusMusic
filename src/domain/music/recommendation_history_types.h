#pragma once
#include "domain/account/session_types.h"
#include "music_types.h"
#include <QDate>
#include <QStringList>
#include <utility>

namespace cirrus::domain
{
// Exact provider calendar labels, not local dates, timestamps, or refresh deadlines.
inline bool validRecommendationDate(const QString& date)
{
    if (date.size() != 10 || date[4] != u'-' || date[7] != u'-')
        return false;
    for (int i = 0; i < date.size(); ++i)
        if (i != 4 && i != 7 && (date[i] < u'0' || date[i] > u'9'))
            return false;
    const auto day = QDate::fromString(date, Qt::ISODate);
    return day.isValid() && day.toString(Qt::ISODate) == date;
}
struct DailyHistoryDatesQuery {
    QString accountId;
    [[nodiscard]] bool isValid() const { return validMusicId(accountId); }
};
struct DailyHistoryDates {
    static constexpr int MaximumDates = 366; // local budget, not an eligibility window
    DailyHistoryDatesQuery query;
    QStringList dates; // preserve provider order and repetitions; explicit empty is valid
};
class DailyHistorySongsQuery final
{
  public:
    static std::optional<DailyHistorySongsQuery>
    fromDates(SessionToken context, const DailyHistoryDates& source, QString date)
    {
        if (!context.isValid() || !source.query.isValid() ||
            source.dates.size() > DailyHistoryDates::MaximumDates ||
            !validRecommendationDate(date) || !source.dates.contains(date))
            return std::nullopt;
        for (const auto& label : source.dates)
            if (!validRecommendationDate(label))
                return std::nullopt;
        return DailyHistorySongsQuery(context, source.query.accountId, std::move(date));
    }
    [[nodiscard]] bool isValid() const
    {
        return m_context.isValid() && validMusicId(m_accountId) && validRecommendationDate(m_date);
    }
    [[nodiscard]] SessionToken sourceContext() const { return m_context; }
    [[nodiscard]] const QString& accountId() const { return m_accountId; }
    [[nodiscard]] const QString& date() const { return m_date; }

  private:
    DailyHistorySongsQuery(SessionToken context, QString accountId, QString date)
        : m_context(context), m_accountId(std::move(accountId)), m_date(std::move(date))
    {
    }
    SessionToken m_context;
    QString m_accountId;
    QString m_date; // validated explicit selection; no full source list retained
};
struct DailyHistorySongs {
    DailyHistorySongsQuery query;
    QList<Song> songs; // metadata only, not an entitlement or playable-source assertion
};
} // namespace cirrus::domain
