/*
OBS Audio Video Sync Dock
Copyright (C) 2023 Norihiro Kamae <norihiro@nagater.net>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <obs-module.h>
#include <inttypes.h>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QMainWindow>
#include <obs-frontend-api.h>
#include "plugin-macros.generated.h"
#include "sync-test-dock.hpp"

#define ASSERT_THREAD(type)                                                                     \
	do {                                                                                    \
		if (!obs_in_task_thread(type))                                                  \
			blog(LOG_ERROR, "%s: ASSERT_THREAD failed: Expected " #type, __func__); \
	} while (false)

SyncTestDock::SyncTestDock(QWidget *parent) : QFrame(parent)
{
	QVBoxLayout *mainLayout = new QVBoxLayout();
	QGridLayout *topLayout = new QGridLayout();

	int y = 0;

	resetButton = new QPushButton(obs_module_text("Button.Reset"), this);
	mainLayout->addWidget(resetButton);
	connect(resetButton, &QPushButton::clicked, this, &SyncTestDock::on_reset);

	QLabel *label;

	// 1. Latency (main display)
	label = new QLabel(obs_module_text("Label.Latency"), this);
	label->setProperty("class", "text-large");
	topLayout->addWidget(label, y, 0);

	latencyDisplay = new QLabel("-", this);
	latencyDisplay->setObjectName("latencyDisplay");
	latencyDisplay->setProperty("class", "text-large");
	topLayout->addWidget(latencyDisplay, y++, 1);

	// 2. Frame Drops (moved up after latency)
	label = new QLabel(obs_module_text("Label.FrameDrops"), this);
	topLayout->addWidget(label, y, 0);

	frameDropDisplay = new QLabel("-", this);
	frameDropDisplay->setObjectName("frameDropDisplay");
	topLayout->addWidget(frameDropDisplay, y++, 1);

	// 3. Audio polarity (early/late - moved before video index)
	latencyPolarity = new QLabel("-", this);
	latencyPolarity->setObjectName("latencyPolarity");
	topLayout->addWidget(latencyPolarity, y++, 1);

	// 4. Video Index
	label = new QLabel(obs_module_text("Label.VideoIndex"), this);
	topLayout->addWidget(label, y, 0);

	videoIndexDisplay = new QLabel("-", this);
	videoIndexDisplay->setObjectName("videoIndexDisplay");
	topLayout->addWidget(videoIndexDisplay, y++, 1);

	// 5. Audio Index
	label = new QLabel(obs_module_text("Label.AudioIndex"), this);
	topLayout->addWidget(label, y, 0);

	audioIndexDisplay = new QLabel("-", this);
	audioIndexDisplay->setObjectName("audioIndexDisplay");
	topLayout->addWidget(audioIndexDisplay, y++, 1);

	// 6. NDI Delivery Latency
	label = new QLabel(obs_module_text("Label.NDIDelivery"), this);
	topLayout->addWidget(label, y, 0);

	ndiLatencyDisplay = new QLabel("-", this);
	ndiLatencyDisplay->setObjectName("ndiLatencyDisplay");
	topLayout->addWidget(ndiLatencyDisplay, y++, 1);

	// Hidden elements for backward compatibility (Index used internally)
	indexDisplay = new QLabel("-", this);
	indexDisplay->setObjectName("indexDisplay");
	indexDisplay->hide();

	frequencyDisplay = new QLabel("-", this);
	frequencyDisplay->setObjectName("frequencyDisplay");
	frequencyDisplay->hide();

	mainLayout->addLayout(topLayout);
	setLayout(mainLayout);

	QTimer::singleShot(0, this, [this]() {
		start_output();
		connect_to_ndi_source();
	});
}

SyncTestDock::~SyncTestDock()
{
	disconnect_from_ndi_source();

	if (sync_test) {
		obs_output_stop(sync_test);
		sync_test = nullptr;
	}
}

extern "C" QWidget *create_sync_test_dock()
{
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	return static_cast<QWidget *>(new SyncTestDock(main_window));
}

#define CD_TO_LOCAL(type, name, get_func) \
	type name;                        \
	if (!get_func(cd, #name, &name))  \
		return;

void SyncTestDock::cb_video_marker_found(void *param, calldata_t *cd)
{
	auto *dock = (SyncTestDock *)param;

	CD_TO_LOCAL(video_marker_found_s *, data, calldata_get_ptr);
	video_marker_found_s found = *data;

	QMetaObject::invokeMethod(dock, [dock, found]() { dock->on_video_marker_found(found); });
};

void SyncTestDock::cb_audio_marker_found(void *param, calldata_t *cd)
{
	auto *dock = (SyncTestDock *)param;

	CD_TO_LOCAL(audio_marker_found_s *, data, calldata_get_ptr);
	audio_marker_found_s found = *data;

	QMetaObject::invokeMethod(dock, [dock, found]() { dock->on_audio_marker_found(found); });
};

void SyncTestDock::cb_sync_found(void *param, calldata_t *cd)
{
	auto *dock = (SyncTestDock *)param;

	CD_TO_LOCAL(sync_index *, data, calldata_get_ptr);
	sync_index found = *data;

	QMetaObject::invokeMethod(dock, [dock, found]() { dock->on_sync_found(found); });
}

void SyncTestDock::cb_frame_drop_detected(void *param, calldata_t *cd)
{
	auto *dock = (SyncTestDock *)param;

	CD_TO_LOCAL(frame_drop_event_s *, data, calldata_get_ptr);
	frame_drop_event_s found = *data;

	QMetaObject::invokeMethod(dock, [dock, found]() { dock->on_frame_drop_detected(found); });
}

void SyncTestDock::cb_ndi_timing(void *param, calldata_t *cd)
{
	auto *dock = (SyncTestDock *)param;

	CD_TO_LOCAL(ndi_timing_info_t *, data, calldata_get_ptr);
	ndi_timing_info_t timing = *data;

	QMetaObject::invokeMethod(dock, [dock, timing]() { dock->on_ndi_timing(timing); });
}

void SyncTestDock::start_output()
{
	OBSOutputAutoRelease o = obs_output_create(OUTPUT_ID, "sync-test-output", nullptr, nullptr);
	if (!o) {
		blog(LOG_ERROR, "Failed to create sync-test-output.");
		return;
	}

	last_video_ix = last_audio_ix = -1;
	missed_video_ix = missed_audio_ix = 0;
	received_video_ix = received_audio_ix = 0;
	received_video_index_max = 256;
	received_audio_index_max = 256;
	audio_index_max = 256;
	total_frame_drops = 0;
	total_frames_seen = 0;
	last_summary_ts = 0;
	last_debug_log_ts = 0;
	sync_count_since_summary = 0;
	latency_sum_since_summary = 0.0;

	auto *sh = obs_output_get_signal_handler(o);
	signal_handler_connect(sh, "video_marker_found", cb_video_marker_found, this);
	signal_handler_connect(sh, "audio_marker_found", cb_audio_marker_found, this);
	signal_handler_connect(sh, "sync_found", cb_sync_found, this);
	signal_handler_connect(sh, "frame_drop_detected", cb_frame_drop_detected, this);

	bool success = obs_output_start(o);

	if (!success)
		latencyPolarity->setText(obs_module_text("Display.Polarity.Failure"));

	sync_test = o;
}

void SyncTestDock::on_reset()
{
	if (sync_test) {
		obs_output_stop(sync_test);
		sync_test = nullptr;
	}

	latencyDisplay->setText("-");
	latencyPolarity->setText("-");
	indexDisplay->setText("-");
	frequencyDisplay->setText("-");
	videoIndexDisplay->setText("-");
	audioIndexDisplay->setText("-");
	frameDropDisplay->setText("-");
	ndiLatencyDisplay->setText("-");

	ndi_latency_sum_ns = 0;
	ndi_latency_count = 0;

	disconnect_from_ndi_source();
	start_output();
	connect_to_ndi_source();
}

static int missed_markers(int index, int last_index, int max_index)
{
	if (index == last_index + 1 || last_index < 0 || max_index <= 0)
		return 0;
	return (max_index + index - last_index - 1) % max_index;
}

void SyncTestDock::on_video_marker_found(struct video_marker_found_s data)
{
	const int index = data.qr_data.index;
	missed_video_ix += missed_markers(index, last_video_ix, received_video_index_max);
	last_video_ix = index;
	received_video_index_max = data.qr_data.index_max;
	received_video_ix++;
	total_frames_seen++;
	frequencyDisplay->setText(QStringLiteral("%1 Hz").arg(data.qr_data.f));
	int missed = missed_video_ix * 100 / (received_video_ix + missed_video_ix);
	videoIndexDisplay->setText(QStringLiteral("%1 (%2% missed)").arg(index).arg(missed));

	if (total_frame_drops == 0 && total_frames_seen > 0)
		frameDropDisplay->setText(QStringLiteral("0 dropped (0.0%)"));
}

void SyncTestDock::on_audio_marker_found(struct audio_marker_found_s data)
{
	const int index = data.index;
	missed_audio_ix += missed_markers(index, last_audio_ix, received_audio_index_max);
	last_audio_ix = index;
	received_audio_index_max = data.index_max;
	received_audio_ix++;
	int missed = missed_audio_ix * 100 / (received_audio_ix + missed_audio_ix);
	audioIndexDisplay->setText(QStringLiteral("%1 (%2% missed)").arg(index).arg(missed));
}

void SyncTestDock::on_sync_found(sync_index data)
{
	int64_t ts = (int64_t)data.audio_ts - (int64_t)data.video_ts;
	double latency_ms = ts * 1e-6;
	latencyDisplay->setText(QStringLiteral("%1 ms").arg(latency_ms, 2, 'f', 1));
	indexDisplay->setText(QStringLiteral("%1").arg(data.index));
	if (ts > 0)
		latencyPolarity->setText(obs_module_text("Display.Polarity.Positive"));
	else if (ts < 0)
		latencyPolarity->setText(obs_module_text("Display.Polarity.Negative"));

	if (data.video_ts - last_debug_log_ts >= 1000000000ULL) {
		blog(LOG_DEBUG, "[sync-dock] latency=%.1f ms  index=%d  video_ts=%llu  audio_ts=%llu",
		     latency_ms, data.index,
		     (unsigned long long)data.video_ts,
		     (unsigned long long)data.audio_ts);
		last_debug_log_ts = data.video_ts;
	}

	sync_count_since_summary++;
	latency_sum_since_summary += latency_ms;

	if (last_summary_ts == 0)
		last_summary_ts = data.video_ts;

	if (data.video_ts - last_summary_ts >= 10000000000ULL) {
		double avg_latency = latency_sum_since_summary / sync_count_since_summary;
		int64_t total = total_frames_seen + total_frame_drops;
		double drop_rate = total > 0 ? (double)total_frame_drops * 100.0 / (double)total : 0.0;
		blog(LOG_INFO, "[sync-dock] avg_latency=%.1f ms  measurements=%d  total_frames=%" PRId64 "  total_drops=%" PRId64 "  drop_rate=%.1f%%",
		     avg_latency, sync_count_since_summary, total_frames_seen, total_frame_drops, drop_rate);
		sync_count_since_summary = 0;
		latency_sum_since_summary = 0.0;
		last_summary_ts = data.video_ts;
	}
}

void SyncTestDock::on_frame_drop_detected(frame_drop_event_s data)
{
	total_frame_drops = (int64_t)data.total_dropped;
	total_frames_seen = (int64_t)data.total_received;
	double drop_rate = 0.0;
	int64_t total = total_frames_seen + total_frame_drops;
	if (total > 0)
		drop_rate = (double)total_frame_drops * 100.0 / (double)total;
	frameDropDisplay->setText(QStringLiteral("%1 dropped (%2%)").arg(total_frame_drops).arg(drop_rate, 0, 'f', 1));

	blog(LOG_DEBUG, "[sync-dock] frame_drop: dropped=%d expected_idx=%d received_idx=%d total_dropped=%" PRId64 " total_received=%" PRId64 " drop_rate=%.1f%%",
	     data.dropped_count, data.expected_index, data.received_index,
	     total_frame_drops, total_frames_seen, drop_rate);
}

void SyncTestDock::on_ndi_timing(ndi_timing_info_t timing)
{
	// Accumulate latency samples and update display every 10 frames
	ndi_latency_sum_ns += timing.pipeline_latency_ns;
	ndi_latency_count++;

	if (ndi_latency_count >= 10) {
		double avg_latency_ms = (double)ndi_latency_sum_ns / (double)ndi_latency_count / 1e6;
		ndiLatencyDisplay->setText(QStringLiteral("%1 ms").arg(avg_latency_ms, 0, 'f', 1));

		// Log periodically (every ~30 frames = ~1 second at 30fps)
		static int log_counter = 0;
		if (++log_counter >= 3) {
			blog(LOG_DEBUG, "[sync-dock] NDI delivery latency=%.1f ms", avg_latency_ms);
			log_counter = 0;
		}

		ndi_latency_sum_ns = 0;
		ndi_latency_count = 0;
	}
}

void SyncTestDock::connect_to_ndi_source()
{
	// Already connected?
	if (ndi_source_ref)
		return;

	// Find the first NDI source and connect to its ndi_timing signal
	obs_enum_sources([](void *param, obs_source_t *source) {
		auto *dock = (SyncTestDock *)param;

		const char *source_id = obs_source_get_id(source);
		if (source_id && strcmp(source_id, "ndi_source") == 0) {
			// Found an NDI source, connect to its signal
			auto *sh = obs_source_get_signal_handler(source);
			signal_handler_connect(sh, "ndi_timing", cb_ndi_timing, dock);

			// Store weak reference for later disconnection
			dock->ndi_source_ref = obs_source_get_weak_source(source);

			blog(LOG_INFO, "[sync-dock] Connected to NDI source '%s' for timing signals",
			     obs_source_get_name(source));
			return false; // Stop enumeration
		}
		return true; // Continue enumeration
	}, this);

	// If no NDI source found, retry after a delay (sources may not be loaded yet)
	if (!ndi_source_ref) {
		blog(LOG_DEBUG, "[sync-dock] No NDI source found, will retry in 2 seconds");
		QTimer::singleShot(2000, this, [this]() { connect_to_ndi_source(); });
	}
}

void SyncTestDock::disconnect_from_ndi_source()
{
	if (!ndi_source_ref)
		return;

	OBSSourceAutoRelease source = obs_weak_source_get_source(ndi_source_ref);
	if (source) {
		auto *sh = obs_source_get_signal_handler(source);
		signal_handler_disconnect(sh, "ndi_timing", cb_ndi_timing, this);
		blog(LOG_DEBUG, "[sync-dock] Disconnected from NDI source timing signals");
	}

	ndi_source_ref = nullptr;
}
