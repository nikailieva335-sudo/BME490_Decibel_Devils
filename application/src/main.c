#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <math.h>

LOG_MODULE_REGISTER(mic_db, LOG_LEVEL_INF); // Register a logging module named "mic_db"

#define NUM_SAMPLES     256   // Number of samples to read in one window
#define SAMPLE_DELAY_US 100   // Delay between samples in microseconds
#define CAL_WINDOWS     50    // Number of windows to read for calibration


static struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

static int16_t samples[NUM_SAMPLES];
static int16_t buf;
static struct adc_sequence sequence = {
	.buffer = &buf,
	.buffer_size = sizeof(buf),
};


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

int main(void)
{
	int err;

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


	int32_t full_scale_mv = 4095;

	adc_raw_to_millivolts_dt(&adc_channel, &full_scale_mv);
	float mv_per_step = full_scale_mv / 4095.0f;


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
		err = read_window();
		if (err < 0) {
			LOG_ERR("Could not read (%d)", err);
			k_msleep(50);
			continue;
		}

		float rms = window_rms();

		if (rms < 0.01f) {
			rms = 0.01f;  /* log10(0) is undefined */
		}

		float db = 20.0f * log10f(rms / rms_ref);

		LOG_INF("Vrms: %6.2f mV   level: %5.1f dB",
			(double)(rms * mv_per_step), (double)db);
	}

	return 0;
}