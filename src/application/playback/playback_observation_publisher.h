#pragma once
#include "application/playback/listening_session.h"
#include "domain/playback/playback_observation_types.h"
#include <QPointer>
#include <QTimer>

namespace cirrus::application
{
// Owning-thread, value-only projection. Owner/facade must share this thread.
// No I/O, automatic lookup, persistence, playback commands or second media clock.
class PlaybackObservationPublisher final : public QObject
{
    Q_OBJECT
  public:
    PlaybackObservationPublisher(ListeningSession& owner, BackendFacade& backend,
                                 QObject* parent = nullptr);
    ~PlaybackObservationPublisher() override { close(); }
    [[nodiscard]] domain::PlaybackObservation snapshot() const;
    [[nodiscard]] std::optional<domain::PlaybackLyricDocument>
    document(domain::ObservationBinding binding, quint64 revision) const;
    bool attachMetadata(domain::ObservationBinding binding, const domain::Song& song);
    using OptionalDocument = std::optional<domain::PlaybackLyricDocument>;
    struct ValidatedExport {
        domain::PlaybackObservation snapshot;
        OptionalDocument document;
    };
    // Owner-thread factory: rechecks ORIGINAL private source/context now, not a
    // historical changed argument. No export during a partially sampled load.
    [[nodiscard]] std::optional<ValidatedExport> validatedExport() const;
    // New one-shot service instance, without commands/queries. Does not undo any
    // sticky private/policy retirement; already observed position age is not renewed.
    domain::ObservationBinding restartExportInstance();
    void retireExports(); // explicit exposure barrier; old load stays retired until new intent
    void close();         // one-shot, does not stop/sign out the owner
    static constexpr qsizetype MaximumTextUnits = 1024 * 1024;
    static constexpr qsizetype MaximumCursorEntries = 32768;
  signals:
    void changed(cirrus::domain::PlaybackObservation value);
    // Synchronous private barrier, including before the first coherent sample.
    // Only public old binding crosses this seam; never SessionToken/LoadToken.
    void contextRetired(cirrus::domain::ObservationBinding previous);

  private:
    bool contextValid() const;
    bool cacheCurrent() const;
    void schedule(bool urgent);
    void capture();
    void invalidateContext(domain::SessionSnapshot fingerprint);
    void retireCurrentView(bool retireLoad);
    domain::PlaybackObservation empty() const;
    QPointer<ListeningSession> m_owner;
    QPointer<BackendFacade> m_backend;
    QTimer m_flush;
    qint64 m_clockOrigin = 0, m_lastSample = -250;
    bool m_closed = false, m_contentDirty = true, m_hasSample = false, m_retiring = false;
    domain::SessionSnapshot m_fingerprint;    // private; never exported
    domain::LoadToken m_load;                 // private; never exported
    domain::LoadToken m_retiredLoad;          // sticky across phase changes/back, bounded one value
    std::optional<quint64> m_retiredRevision; // private owner load-intent boundary, never wire
    domain::PlaybackSnapshot m_seen;
    domain::PlaybackObservation m_value;
    std::optional<domain::PlaybackLyricDocument> m_document;
    std::optional<domain::PlaybackMetadataView> m_metadata;
    domain::ObservationContentStatus m_metadataStatus{};
    quint64 m_sequence = 0, m_lyricsRevision = 0;
};
} // namespace cirrus::application
