#pragma once
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <obs.hpp>
#include "sync-test-output.hpp"

// NDI timing information received from DistroAV ndi_source
// Must match the struct definition in ndi-source.cpp
// Fields are ordered in frame processing order for clarity
typedef struct ndi_timing_info_t {
	// Input values
	int64_t ndi_timecode_ns;       // 1. Raw NDI PTP capture time from sender
	int64_t clock_offset_ns;       // 2. Conversion factor: wall_clock - obs_clock
	int64_t buffer_ns;             // 3. User's buffer setting in nanoseconds

	// Computed values
	int64_t presentation_ns;       // 4. OBS presentation timestamp = ndi_tc - clock_offset + buffer
	int64_t obs_now_ns;            // 5. Current OBS monotonic time at signal emission

	// Derived metrics
	int64_t ts_ahead_ns;           // 6. presentation - obs_now (buffer headroom, >0 = future)
	int64_t pipeline_latency_ns;   // 7. wall_now - ndi_timecode (network + processing delay)

	// Debug
	uint64_t frame_number;         // Sequential video frame counter
} ndi_timing_info_t;

class SyncTestDock : public QFrame {
	Q_OBJECT

public:
	SyncTestDock(QWidget *parent = nullptr);
	~SyncTestDock();

private:
	QPushButton *resetButton = nullptr;

	QLabel *latencyDisplay = nullptr;
	QLabel *latencyPolarity = nullptr;
	QLabel *indexDisplay = nullptr;
	QLabel *frequencyDisplay = nullptr;
	QLabel *videoIndexDisplay = nullptr;
	QLabel *audioIndexDisplay = nullptr;
	QLabel *frameDropDisplay = nullptr;
	QLabel *ndiReleaseDisplay = nullptr;
	QLabel *ndiReceiveDisplay = nullptr;

	// NDI timing detail displays (in processing order)
	QLabel *ndiTimecodeDisplay = nullptr;
	QLabel *clockOffsetDisplay = nullptr;
	QLabel *bufferDisplay = nullptr;
	QLabel *presentationDisplay = nullptr;
	QLabel *obsNowDisplay = nullptr;
	QLabel *tsAheadDisplay = nullptr;
	QLabel *pipelineDisplay = nullptr;
	QLabel *frameNumberDisplay = nullptr;

private:
	OBSOutput sync_test;
	OBSWeakSource ndi_source_ref;

private:
	int last_video_ix;
	int last_audio_ix;
	int missed_video_ix;
	int missed_audio_ix;
	int received_video_ix;
	int received_audio_ix;
	int received_video_index_max = 0;
	int received_audio_index_max = 0;
	int audio_index_max = 0;
	int64_t total_frame_drops = 0;
	int64_t total_frames_seen = 0;

	uint64_t last_summary_ts = 0;
	uint64_t last_debug_log_ts = 0;
	int sync_count_since_summary = 0;
	double latency_sum_since_summary = 0.0;

	// NDI timing tracking (averaged over multiple frames)
	int64_t ndi_release_sum_ns = 0;    // presentation - ndi_timecode (capture to present)
	int64_t ndi_receive_sum_ns = 0;    // pipeline_latency (capture to receive)
	int ndi_timing_count = 0;

	// Throttle timing detail display to once per second
	uint64_t last_timing_display_update_ns = 0;

private:
	void start_output();
	void on_reset();
	void connect_to_ndi_source();
	void disconnect_from_ndi_source();

	void on_video_marker_found(video_marker_found_s data);
	void on_audio_marker_found(audio_marker_found_s data);
	void on_sync_found(sync_index data);
	void on_frame_drop_detected(frame_drop_event_s data);
	void on_ndi_timing(ndi_timing_info_t timing);

	static void cb_video_marker_found(void *param, calldata_t *cd);
	static void cb_audio_marker_found(void *param, calldata_t *cd);
	static void cb_sync_found(void *param, calldata_t *cd);
	static void cb_frame_drop_detected(void *param, calldata_t *cd);
	static void cb_ndi_timing(void *param, calldata_t *cd);
};
