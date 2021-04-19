#include <soundio/soundio.h>
#include <sndfile.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

int record_duration = 30;
int sample_rate = 0;
int channel_count = 0;
enum SoundIoFormat fmt = SoundIoFormatInvalid;

enum SoundIoFormat prioritized_formats[] = {
    SoundIoFormatFloat32NE,
    SoundIoFormatFloat32FE,
    SoundIoFormatS32NE,
    SoundIoFormatS32FE,
    SoundIoFormatS24NE,
    SoundIoFormatS24FE,
    SoundIoFormatS16NE,
    SoundIoFormatS16FE,
    SoundIoFormatFloat64NE,
    SoundIoFormatFloat64FE,
    SoundIoFormatU32NE,
    SoundIoFormatU32FE,
    SoundIoFormatU24NE,
    SoundIoFormatU24FE,
    SoundIoFormatU16NE,
    SoundIoFormatU16FE,
    SoundIoFormatS8,
    SoundIoFormatU8,
    SoundIoFormatInvalid,
};

int prioritized_sample_rates[] = {
    48000,
    44100,
    96000,
    24000,
    0,
};

struct Buffer {
	char *begin;
	char *end;
	char *wptr;
	char *rptr;
	int size;
}buf;

int usage(char *exe) {
    fprintf(stderr, "Usage: %s [options]\n"
            "Options:\n"
            "  [--list-devices]\n"
            "  [--device id]\n"
            "  [--duration seconds]\n"
            , exe);
    return 1;
}


void print_device(struct SoundIoDevice *device, bool is_default) {
	const char *default_str = is_default ? " (default)" : "";
	const char *raw_str = device->is_raw ? " (raw)" : "";
	fprintf(stderr, "%s%s%s\n", device->name, default_str, raw_str);
	fprintf(stderr, "  id: %s\n\n", device->id);
}

void list_devices(struct SoundIo *soundio) {
    int output_count = soundio_output_device_count(soundio);
    int input_count = soundio_input_device_count(soundio);

    int default_output = soundio_default_output_device_index(soundio);
    int default_input = soundio_default_input_device_index(soundio);

    fprintf(stderr, "--------Input Devices--------\n\n");
    for (int i = 0; i < input_count; i += 1) {
        struct SoundIoDevice *device = soundio_get_input_device(soundio, i);
        print_device(device, default_input == i);
        soundio_device_unref(device);
    }
    fprintf(stderr, "--------Output Devices--------\n\n");
    for (int i = 0; i < output_count; i += 1) {
        struct SoundIoDevice *device = soundio_get_output_device(soundio, i);
        print_device(device, default_output == i);
        soundio_device_unref(device);
    }

    fprintf(stderr, "%d devices found\n\n", input_count + output_count);
}

void error(char *error_message) {
	fprintf(stderr, "%s\n", error_message);
	exit(1);
}

void error_msg(char *error_message, int err) {
	fprintf(stderr, "%s %s\n", error_message, soundio_strerror(err));
	exit(1);
}

void error_msg_sf(char *error_message, SNDFILE *sndfile) {
	fprintf(stderr, "%s %s\n", error_message, sf_strerror(sndfile));
	exit(1);
}

bool is_format_floating_point(enum SoundIoFormat format) {
	return (format == SoundIoFormatFloat32NE ||
					format == SoundIoFormatFloat32FE ||
					format == SoundIoFormatFloat64NE ||
					format == SoundIoFormatFloat64FE);
}

void read_callback(struct SoundIoInStream *instream, int frame_count_min, int frame_count_max) {
	struct Buffer *buf = instream->userdata;
	struct SoundIoChannelArea *areas;
	int err;

	int frames_left = frame_count_max;

	while (true) {
		int frame_count = frames_left;

		if ((err = soundio_instream_begin_read(instream, &areas, &frame_count)))
			error_msg("Begin read error: ", err);

		if (!frame_count)
			break;

		if (!areas) {
			// There is a hole, so fill buffer with silence
			for (int i = 0; i < frame_count; i += 1) {
				memset(buf->wptr, 0, instream->bytes_per_frame);
				buf->wptr += instream->bytes_per_frame;
				buf->rptr += instream->bytes_per_frame;
				if (buf->wptr == buf->end)
					buf->wptr = buf->begin;
				if (buf->rptr == buf->end)
					buf->rptr = buf->begin;
			}
		} else {
			for (int frame = 0; frame < frame_count; frame += 1) {
				for (int ch = 0; ch < instream->layout.channel_count; ch += 1) {
					memcpy(buf->wptr, areas[ch].ptr, instream->bytes_per_sample);
					buf->wptr += instream->bytes_per_sample;
					areas[ch].ptr += areas[ch].step;
					if (buf->wptr == buf->end)
						buf->wptr = buf->begin;
				}
				buf->rptr += instream->bytes_per_frame;
				if (buf->rptr == buf->end)
					buf->rptr = buf->begin;
			}
		}

		if ((err = soundio_instream_end_read(instream)))
			error_msg("end read error: ", err);

		frames_left -= frame_count;
		if (frames_left <= 0)
			break;
	}
}

void save_recording(int signo) {
	printf("Caught signal %d\n", signo);

	//FILE *out_file = fopen("outfile", "wb");
	SNDFILE *out_file;
	SF_INFO sf_info;

	memset(&sf_info, 0, sizeof(sf_info));

	sf_info.samplerate	= sample_rate;
	sf_info.frames			= record_duration * sample_rate;
	sf_info.channels		= channel_count;
	sf_info.format			= (SF_FORMAT_WAV | SF_FORMAT_PCM_24) ;

	
	time_t raw_time = time(NULL);
	struct tm *time_info = localtime(&raw_time);
	int max_file_name_size = 40;
	char out_file_name[max_file_name_size];
	strftime(out_file_name, max_file_name_size, "%Y-%m-%d-%H_%M_%S", time_info);
	strcat(out_file_name, ".wav");

	char *tmp = strdup(out_file_name);
	strcpy(out_file_name, "replay-");
	strcat(out_file_name, tmp);

	fprintf(stderr, "%s\n", out_file_name);

	if ( (out_file = sf_open(out_file_name, SFM_WRITE, &sf_info)) == NULL )
		error_msg_sf("Error opening output file: ", NULL);

	int buffer_size = buf.size;
	char *buffer = malloc(buffer_size);
	if (!buffer)
		error("Error allocating memory");

	char *ptr = buf.rptr;
	for (int i = 0; i < buffer_size; i += 1) {
		buffer[i] = *ptr;
		ptr++;
		if (ptr == buf.end)
			ptr = buf.begin;
	}

	/*
	for (int i = 0; i < buffer_size; i += 1) {
		fputc(buffer[i], out_file);
	}
	*/

	unsigned long bytes_per_sample = soundio_get_bytes_per_sample(fmt);
	unsigned long bytes_per_frame = bytes_per_sample * channel_count;
	bool is_floating_point = is_format_floating_point(fmt);
	int frame_count = buffer_size / bytes_per_frame;

	int ret = 0;
	if (is_floating_point) {
		if (bytes_per_sample <= sizeof(float))
			ret = sf_writef_float(out_file, (float*)buffer, frame_count);
		else if (bytes_per_sample <= sizeof(double))
			ret = sf_writef_double(out_file, (double*)buffer, frame_count);
		else 
			error("Write error: No sufficiently large floating point data type available for encoding\n");
	} else {
		if (bytes_per_sample <= sizeof(short))
			ret = sf_writef_short(out_file, (short*)buffer, frame_count);
		else if (bytes_per_sample <= sizeof(int))
			ret = sf_writef_int(out_file, (int*)buffer, frame_count);
		else 
			error("Write error: No sufficiently large data type available for encoding\n");
	}

	if (ret != frame_count)
		error_msg_sf("Error writing to output file: ", NULL);

	free(buffer);
	sf_close(out_file);

	/*
	char *ptr = buf.wptr;
	do {
		fputc(*ptr, out_file);
		ptr++;
		if (ptr == buf.end)
		ptr = buf.begin;
	} while(ptr != buf.wptr);
	*/

	//fclose(out_file);
}

int main (int argc, char *argv[]) {
	char *exe = argv[0];
	char *device_id = NULL;
	bool do_list_devices = false;

	for (int i = 1; i < argc; i += 1) {
		char *arg = argv[i];
		if (arg[0] == '-' && arg[1] == '-') {
			if (strcmp(arg, "--list-devices") == 0) {
				do_list_devices = true;
			} else if (++i >= argc) {
				return usage(exe);
			} else if (strcmp(arg, "--device") == 0) {
				device_id = argv[i];
			} else if (strcmp(arg, "--duration") == 0) {
				char *endptr = NULL;
				record_duration = strtol(argv[i], &endptr, 10);
				if (*endptr != 0)
					return usage(exe);
			} else {
				return usage(exe);
			}
		}
	}

	int err;

	struct SoundIo *soundio = soundio_create();
	if (!soundio)
		error("Out of memory");

	if ((err = soundio_connect(soundio))) 
		error_msg("Error connecting: ", err);

	soundio_flush_events(soundio);

	if (do_list_devices)
		list_devices(soundio);

	struct SoundIoDevice *selected_device = NULL;

	if (device_id) {
		for (int i = 0; i < soundio_input_device_count(soundio); i += 1) {
			struct SoundIoDevice *device = soundio_get_input_device(soundio, i);
			if (strcmp(device->id, device_id) == 0) {
				selected_device = device;
				break;
			}
			soundio_device_unref(device);
		}
		if (!selected_device) {
			fprintf(stderr, "Invalid device id: %s\n", device_id);
			return 1;
		}
	} else {
		int device_index = soundio_default_input_device_index(soundio);
		selected_device = soundio_get_input_device(soundio, device_index);
		if (!selected_device)
			error("No input devices available.\n");
	}

	/*
	int in_device_index = soundio_default_input_device_index(soundio);
	struct SoundIoDevice *selected_device = soundio_get_input_device(soundio, in_device_index);
	if (!selected_device) 
		error("No input devices available");
	*/

	fprintf(stderr, "--------Selected Input Device--------\n\n");
	fprintf(stderr, "%s\n", selected_device->name);

	if (selected_device->probe_error)
		error_msg("Unable to probe device: ", selected_device->probe_error);

	soundio_device_sort_channel_layouts(selected_device);

	int *sample_rate_ptr;
	for (sample_rate_ptr = prioritized_sample_rates; *sample_rate_ptr; sample_rate_ptr += 1) {
		if (soundio_device_supports_sample_rate(selected_device, *sample_rate_ptr)) {
			sample_rate = *sample_rate_ptr;
			break;
		}
	}
	if (!sample_rate)
		sample_rate = selected_device->sample_rates[0].max;

	fmt = SoundIoFormatInvalid;
	enum SoundIoFormat *fmt_ptr;
	for (fmt_ptr = prioritized_formats; *fmt_ptr != SoundIoFormatInvalid; fmt_ptr += 1) {
		if (soundio_device_supports_format(selected_device, *fmt_ptr)) {
			fmt = *fmt_ptr;
			break;
		}
	}
	if (fmt == SoundIoFormatInvalid)
		fmt = selected_device->formats[0];

	struct SoundIoInStream *instream = soundio_instream_create(selected_device);
	if (!instream)
		error("Out of memory");

	instream->format = fmt;
	instream->sample_rate = sample_rate;
	instream->read_callback = read_callback;
	//instream->overflow_callback = overflow_callback;
	instream->userdata = &buf;

	if ((err = soundio_instream_open(instream)))
		error_msg("Unable to open input stream: ", err);

	fprintf(stderr, "%s %dHz %s interleaved\n\n",
			instream->layout.name, instream->sample_rate, soundio_format_string(fmt));

	channel_count = instream->layout.channel_count;

	buf.size = record_duration * instream->sample_rate * instream->bytes_per_frame;
	buf.begin = malloc(buf.size);
	if (!buf.begin)
		error("Error allocating memory");
	buf.wptr = buf.begin;
	buf.rptr = buf.begin;
	buf.end = buf.begin + buf.size;

	if ((err = soundio_instream_start(instream)))
		error_msg("Unable to start input device: ", err);

	if (signal(SIGUSR1, save_recording) == SIG_ERR)
		error("Can't handle SIGUSR1");

	for (;;)
		soundio_wait_events(soundio);

	free(buf.begin);
	soundio_instream_destroy(instream);
	soundio_device_unref(selected_device);
	soundio_destroy(soundio);

	return 0;
}
