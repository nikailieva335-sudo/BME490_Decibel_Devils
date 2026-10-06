#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <math.h>
#include "ble_lib.h"


LOG_MODULE_REGISTER(mic_db, LOG_LEVEL_INF); // Register a logging module named "mic_db"

#define NUM_SAMPLES     256   // Number of samples to read in one window
#define SAMPLE_DELAY_US 100   // Delay between samples in microseconds
#define CAL_WINDOWS     50    // Number of windows to read for calibration (~2 seconds at 256 samples/window and 100us/sample)

#define SEND_DATA_WINDOWS 5 // Number of windows to read before sending data over BLE

static struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

static int16_t samples[NUM_SAMPLES]; 	// Raw ADC readings
static int16_t buf; 					// Buffer for ADC reading
static struct adc_sequence sequence = { // ADC sequence configuration
	.buffer = &buf,
	.buffer_size = sizeof(buf),
};


// Reads one window of samples into an array and returns 0 on success or a negative error code on failure.
static int read_window(void)
{
	for (int i = 0; i < NUM_SAMPLES; i++) {
		int err = adc_read(adc_channel.dev, &sequence);

		if (err < 0) {
			return err;
		}
		samples[i] = buf;
		k_usleep(SAMPLE_DELAY_US);
	}
	return 0;
}

// Calculates the RMS value of the samples in the current window. Returns the RMS value as a float.
static float window_rms(void)
{
	float mean = 0.0f;
	float sum_sq = 0.0f;

	for (int i = 0; i < NUM_SAMPLES; i++) {
		mean += samples[i];
	}
	mean /= NUM_SAMPLES;

	for (int i = 0; i < NUM_SAMPLES; i++) {
		float d = samples[i] - mean;

		sum_sq += d * d;
	}

	return sqrtf(sum_sq / NUM_SAMPLES);
}

/* -------------------- FREQUENCY MEASUREMENT (PEAK COUNT) -------------------- */
// Counts positive local peaks above a noise-relative threshold in one window.
static int window_peak_count(void)
{
	float mean = 0.0f;
	float sum_sq = 0.0f;

	for (int i = 0; i < NUM_SAMPLES; i++) {
		mean += samples[i];
	}
	mean /= NUM_SAMPLES;

	for (int i = 0; i < NUM_SAMPLES; i++) {
		float deviation = samples[i] - mean;
		sum_sq += deviation * deviation;
	}

	float threshold = mean + 0.25f * sqrtf(sum_sq / NUM_SAMPLES);
	int peaks = 0;

	for (int i = 1; i < NUM_SAMPLES - 1; i++) {
		if (samples[i] > threshold &&
			samples[i] >= samples[i - 1] &&
			samples[i] > samples[i + 1]) {
			peaks++;
		}
	}

	return peaks;
}

// Converts the number of detected peaks to Hz using the nominal sample period.
static float window_frequency_hz(void)
{
	float window_seconds = (NUM_SAMPLES * SAMPLE_DELAY_US) / 1000000.0f;

	return window_peak_count() / window_seconds;
}
/* ------------------ END FREQUENCY MEASUREMENT (PEAK COUNT) ------------------ */

// Initializates ADC, calibrates and logs the sound level continuously in dB. Returns 0 on success or a negative error code on failure.

int main(void)
{
	int err;

	err = bluetooth_init(NULL);
	if (err) {
		LOG_ERR("Bluetooth initialization failed (%d)", err);
		return 0;
	}

	if (!adc_is_ready_dt(&adc_channel)) {
		LOG_ERR("ADC controller device %s not ready", adc_channel.dev->name);
		return 0;
	}

	err = adc_channel_setup_dt(&adc_channel);
	if (err < 0) {
		LOG_ERR("Could not setup channel #%d (%d)", 0, err);
		return 0;
	}

	err = adc_sequence_init_dt(&adc_channel, &sequence);
	if (err < 0) {
		LOG_ERR("Could not initialize sequence (%d)", err);
		return 0;
	}


	// Unit conversion from mV to relative dB
	int32_t full_scale_mv = 4095;

	adc_raw_to_millivolts_dt(&adc_channel, &full_scale_mv);
	float mv_per_step = full_scale_mv / 4095.0f;

	// Calibration
	LOG_INF("Calibrating - keep quiet for ~2 seconds...");
	k_msleep(500);

	float ref_sum = 0.0f;

	for (int w = 0; w < CAL_WINDOWS; w++) {
		err = read_window();
		if (err < 0) {
			LOG_ERR("Could not read during calibration (%d)", err);
			return 0;
		}
		ref_sum += window_rms();
	}

	float rms_ref = ref_sum / CAL_WINDOWS;

	if (rms_ref < 0.5f) {
		rms_ref = 0.5f;
	}

	LOG_INF("Reference Vrms: %.2f mV", (double)(rms_ref * mv_per_step));


	while (1) {
		float rms_sum = 0.0f;
		float frequency_sum = 0.0f;
		int good_windows = 0;

		for (int w = 0; w < SEND_DATA_WINDOWS; w++) {
			err = read_window();
			if (err < 0) {
				LOG_ERR("Could not read (%d)", err);
				bluetooth_set_errors(ERR_ADC_READ);
				continue;
			}
			rms_sum += window_rms();
			frequency_sum += window_frequency_hz();
			good_windows++;
		}

		if (good_windows == 0) {
			k_msleep(50);
			continue;
		}

		float rms = rms_sum / good_windows;
		float frequency_hz = frequency_sum / good_windows;

		if (rms < 0.01f) {
			rms = 0.01f;  /* log10(0) is undefined */
		}

		float db = 20.0f * log10f(rms / rms_ref);

		LOG_INF("Vrms: %6.2f mV   level: %5.1f dB   frequency: %5.0f Hz",
			(double)(rms * mv_per_step), (double)db, (double)frequency_hz);

		/* Round to the nearest tenth of a dB, e.g. 12.43 -> 124 */
		int32_t db10 = (int32_t)(db * 10.0f + (db >= 0.0f ? 0.5f : -0.5f));

		err = bluetooth_send_sound_level(db10, (int32_t)(frequency_hz + 0.5f));
		if (err) {
			LOG_WRN("BLE send failed (%d)", err);
		}
	}

	return 0;
}
