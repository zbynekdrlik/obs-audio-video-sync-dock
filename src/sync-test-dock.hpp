#pragma once
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <obs.hpp>
#include "sync-test-output.hpp"

// NDI timing information received from DistroAV ndi_source
// Must match the struct definition in ndi-source.cpp
typedef struct ndi_timing_info_t {
	int64_t ndi_timecode_ns;      // Raw NDI PTP capture time (nanoseconds)
	int64_t presentation_ns;       // OBS presentation timestamp (nanoseconds)
	int64_t pipeline_latency_ns;   // wall_clock - ndi_timecode (network + processing delay)
	int64_t ts_ahead_ns;          // presentation - obs_now (buffer headroom)
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
	QLabel *ndiAlignedDisplay = nullptr;
	QLabel *ndiRawLatencyDisplay = nullptr;

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

	// NDI latency tracking (averaged over multiple frames)
	int64_t ndi_aligned_sum_ns = 0;    // ts_ahead (aligned/buffered timing)
	int64_t ndi_raw_latency_sum_ns = 0; // pipeline_latency (raw capture to receive)
	int ndi_latency_count = 0;

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
