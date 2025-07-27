#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>
#include <opusfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>
		

// from 3ds-examples/audio/opus-decoding START
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
// from 3ds-examples/audio/opus-decoding END


const int MAX_PATH_CHAR_LENGTH = 4096; // max file name seems to be 255, file paths are concatenated filenames
const int MAX_FILES = 26; // max files to display at once

// TODO kinda temporary for debuggging can probably remove later
PrintConsole topConsole, bottomConsole;

// SOURCE 3ds-examples/audio/opus-decoding (FOR producer consumer design pattern) START
ndspWaveBuf s_waveBufs[3];
int16_t *s_audioBuffer = NULL;
static const int SAMPLE_RATE = 48000;            // Opus is fixed at 48kHz
static const int SAMPLES_PER_BUF = SAMPLE_RATE * 120 / 1000;  // 120ms buffer
static const int CHANNELS_PER_SAMPLE = 2;        // We ask libopusfile for
                                                 // stereo output; it will down
                                                 // -mix for us as necessary.

static const int THREAD_AFFINITY = -1;           // Execute thread on any core
static const int THREAD_STACK_SZ = 32 * 1024;    // 32kB stack for audio thread

static const size_t WAVEBUF_SIZE = SAMPLES_PER_BUF * CHANNELS_PER_SAMPLE
    * sizeof(int16_t);                           // Size of NDSP wavebufs
// SOURCE 3ds-examples/audio/opus-decoding (FOR producer consumer design pattern) END

// producer consumer design pattern START
volatile bool run_thread = true;

struct OpusController {
    std::string songPath;
    OggOpusFile *file;
    volatile bool songReady;
    volatile bool stopPlayback;     
    LightEvent startEvent;      // signal to start playback 
    LightEvent doneEvent;       // signal that song is done playing
    LightEvent fillBufferEvent; // signal to fill buffer
};

OpusController opus_controller = {
    .songPath = "",
    .file = nullptr,
    .songReady = false,
    .stopPlayback = false,
    .startEvent = {0},
    .doneEvent = {0},
    .fillBufferEvent = {0}
};

// SOURCE 3ds-examples/audio/opus-decoding (FOR producer consumer design pattern) START
// Retrieve strings for libopusfile errors
// Sourced from David Gow's example code: https://davidgow.net/files/opusal.cpp
const char *opusStrError(int error)
{
    switch(error) {
        case OP_FALSE:
            return "OP_FALSE: A request did not succeed.";
        case OP_HOLE:
            return "OP_HOLE: There was a hole in the page sequence numbers.";
        case OP_EREAD:
            return "OP_EREAD: An underlying read, seek or tell operation "
                   "failed.";
        case OP_EFAULT:
            return "OP_EFAULT: A NULL pointer was passed where none was "
                   "expected, or an internal library error was encountered.";
        case OP_EIMPL:
            return "OP_EIMPL: The stream used a feature which is not "
                   "implemented.";
        case OP_EINVAL:
            return "OP_EINVAL: One or more parameters to a function were "
                   "invalid.";
        case OP_ENOTFORMAT:
            return "OP_ENOTFORMAT: This is not a valid Ogg Opus stream.";
        case OP_EBADHEADER:
            return "OP_EBADHEADER: A required header packet was not properly "
                   "formatted.";
        case OP_EVERSION:
            return "OP_EVERSION: The ID header contained an unrecognised "
                   "version number.";
        case OP_EBADPACKET:
            return "OP_EBADPACKET: An audio packet failed to decode properly.";
        case OP_EBADLINK:
            return "OP_EBADLINK: We failed to find data we had seen before or "
                   "the stream was sufficiently corrupt that seeking is "
                   "impossible.";
        case OP_ENOSEEK:
            return "OP_ENOSEEK: An operation that requires seeking was "
                   "requested on an unseekable stream.";
        case OP_EBADTIMESTAMP:
            return "OP_EBADTIMESTAMP: The first or last granule position of a "
                   "link failed basic validity checks.";
        default:
            return "Unknown error.";
    }
}

// Main audio decoding logic
// This function pulls and decodes audio samples from opusFile_ to fill waveBuf_
bool fillBuffer(OggOpusFile *opusFile_, ndspWaveBuf *waveBuf_) {
    #ifdef DEBUG
    // Setup timer for performance stats
    TickCounter timer;
    osTickCounterStart(&timer);
    #endif  // DEBUG

    // Decode samples until our waveBuf is full
    int totalSamples = 0;
    while(totalSamples < SAMPLES_PER_BUF) {
        int16_t *buffer = waveBuf_->data_pcm16 + (totalSamples *
            CHANNELS_PER_SAMPLE);
        const size_t bufferSize = (SAMPLES_PER_BUF - totalSamples) *
            CHANNELS_PER_SAMPLE;

        // Decode bufferSize samples from opusFile_ into buffer,
        // storing the number of samples that were decoded (or error)
        const int samples = op_read_stereo(opusFile_, buffer, bufferSize);
        if(samples <= 0) {
            if(samples == 0) break;  // No error here

            printf("op_read_stereo: error %d (%s)", samples,
                   opusStrError(samples));
            break;
        }
        
        totalSamples += samples;
    }

    // If no samples were read in the last decode cycle, we're done
    if(totalSamples == 0) {
        printf("Playback complete, press Start to exit\n");
        return false;
    }

    // Pass samples to NDSP
    waveBuf_->nsamples = totalSamples;
    ndspChnWaveBufAdd(0, waveBuf_);
    DSP_FlushDataCache(waveBuf_->data_pcm16,
        totalSamples * CHANNELS_PER_SAMPLE * sizeof(int16_t));

    #ifdef DEBUG
    PrintConsole *prev = consoleSelect(&bottomConsole);
    // Print timing info
    osTickCounterUpdate(&timer);
    printf("fillBuffer %lfms in %lfms\n", totalSamples * 1000.0 / SAMPLE_RATE,
        osTickCounterRead(&timer));
    consoleSelect(prev);
    #endif  // DEBUG

    return true;
}

// Pause until user presses a button
void waitForInput(void) {
    printf("Press any button to exit...\n");
    while(aptMainLoop())
    {
        gspWaitForVBlank();
        gfxSwapBuffers();
        hidScanInput();

        if(hidKeysDown())
            break;
    }
}

// Audio initialisation code
// This sets up NDSP and our primary audio buffer
bool audioInit(void) {
    // Setup NDSP
    ndspChnReset(0);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    // Allocate audio buffer
    const size_t bufferSize = WAVEBUF_SIZE * ARRAY_SIZE(s_waveBufs);
    s_audioBuffer = (int16_t *)linearAlloc(bufferSize);
    if(!s_audioBuffer) {
        printf("Failed to allocate audio buffer\n");
        return false;
    }

    // Setup waveBufs for NDSP
    memset(&s_waveBufs, 0, sizeof(s_waveBufs));
    int16_t *buffer = s_audioBuffer;

    for(size_t i = 0; i < ARRAY_SIZE(s_waveBufs); ++i) {
        s_waveBufs[i].data_vaddr = buffer;
        s_waveBufs[i].status     = NDSP_WBUF_DONE;

        buffer += WAVEBUF_SIZE / sizeof(buffer[0]);
    }

    return true;
}

// Audio de-initialisation code
// Stops playback and frees the primary audio buffer
void audioExit(void) {
    ndspChnReset(0);
    linearFree(s_audioBuffer);
}
// SOURCE 3ds-examples/audio/opus-decoding (FOR producer consumer design pattern) END


void audioThread(void *arg) {
    while (run_thread) {
        // wait until a song is ready to play
        LightEvent_Wait(&opus_controller.startEvent);

        // failsafe if somehow event is signaled when it shouldn't be
        if (!opus_controller.songReady) {
            continue;
        }

        OggOpusFile *file = opus_controller.file;

        while (run_thread && !opus_controller.stopPlayback) {
            for (size_t i=0; i<ARRAY_SIZE(s_waveBufs); ++i) {
                if (s_waveBufs[i].status != NDSP_WBUF_DONE) {
                    continue; 
                }

                // fill the buffer with audio data
                if (!fillBuffer(file, &s_waveBufs[i])) {
                    opus_controller.songReady = false; // song finished playing
                    LightEvent_Signal(&opus_controller.doneEvent);
                    // get outside of while loop until next song is played
                    opus_controller.stopPlayback = true;
                    break;
                }
            }
            LightEvent_Wait(&opus_controller.fillBufferEvent);
        }
        // reset flags
        op_free(opus_controller.file);
        opus_controller.file = nullptr;
        opus_controller.songReady = false;
        opus_controller.stopPlayback = false;

        LightEvent_Signal(&opus_controller.doneEvent); // signal that playback is done
    }
}

bool playSong(std::string path) {
    opus_controller.songPath = path;
    
    int error = 0;
    opus_controller.file = op_open_file(opus_controller.songPath.c_str(), &error);
    if (error || opus_controller.file == nullptr) {
        // TODO maybe have some sort of logging system, maybe log to file later?
        printf("Error opening file: %s\n", opusStrError(error));
        return false;
    }
    opus_controller.songReady = true;
    opus_controller.stopPlayback = false;

    // signal to start playing song
    LightEvent_Signal(&opus_controller.startEvent);

    return true;
}

void opusCallback(void *arg) {
    (void)arg;  // suppress unused parameter warning

    if (!run_thread) {
        return;
    }

    LightEvent_Signal(&opus_controller.fillBufferEvent);
}


// producer consumer design pattern END


std::vector<dirent> get_files(const char* path) {
	std::vector<dirent> file_list;
	DIR* dir = opendir(path);
	if (dir == nullptr) {
		printf("Failed to open directory: %s\n", path);
		return file_list;
	}

	struct dirent* ent;
	while ((ent = readdir(dir)) != nullptr) {
		file_list.push_back(*ent);
	}

	closedir(dir);
	return file_list;
}

void print_files(std::vector<dirent> files, size_t selectedFile, size_t maxFiles = MAX_FILES) {
	for (size_t i = selectedFile; i < std::min(files.size(), (size_t)MAX_FILES+selectedFile); i++) {
		std::string result = "";
		if (i == selectedFile) {
			result += "-> ";
		} else {
			result += "   ";
		}
		result += files[i].d_name;
		if (files[i].d_type == DT_DIR) {
			result += "/";
		}
		printf("%s\n", result.c_str());
		// printf("%s\n", files[i].d_name);
	}
	// printf("%s\n", files[0].d_name);
	// printf("%s\n", files[1].d_name);
}


int main(int argc, char* argv[])
{
	romfsInit();
	gfxInitDefault();

    consoleInit(GFX_TOP, &topConsole);
    consoleInit(GFX_BOTTOM, &bottomConsole);
    // start on top screen
    consoleSelect(&topConsole);
    
    // TODO add a msg telling ppl how to dump with luma3ds (likely bc dspfirm isn't dumped)
	ndspInit();
    
    // Enable N3DS 804MHz operation, where available
    // osSetSpeedupEnable(true);

    // producer consumer design pattern START
    LightEvent_Init(&opus_controller.startEvent, RESET_ONESHOT);
    LightEvent_Init(&opus_controller.doneEvent, RESET_ONESHOT);
	// Attempt audioInit (we only need to initialize once)
    if(!audioInit()) {
        printf("Failed to initialise audio\n");
        waitForInput();

        gfxExit();
        ndspExit();
        romfsExit();
        return EXIT_FAILURE;
    }
    
    // Spawn audio thread

    // main thread priority
    int32_t priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    // lower number => higher actual priority
    priority -= 1;
    // thread priorities must be between 0x18 and 0x3F
    priority = priority < 0x18 ? 0x18 : priority;
    priority = priority > 0x3F ? 0x3F : priority;

    // takes no args as it gets opus filepath from global state struct
    const Thread threadId = threadCreate(audioThread, nullptr,
                                         THREAD_STACK_SZ, priority,
                                         THREAD_AFFINITY, false);

    ndspSetCallback(opusCallback, NULL);
    // producer consumer design pattern END

	// alternate between two buffers to allow for filling one while the other is playing
	// ndspWaveBuf waveBuf[NUM_BUFFERS];
	
	// chdir("/");
	
	// make sure to have trailing '/' character
	const std::string START_PATH = "sdmc:/Music/";
	// used to prevent the user from navigating about the root (`smdc:/` is 6 characters)
	const size_t ROOT_SLASH_IDX = 5;
	
	std::string cwd = START_PATH;
	// TODO try out filesystem, after changing flag in makefile to c++17 it compiles
	// std::filesystem::path cwd_filesystem(START_PATH);
	// std::filesystem::directory_iterator dir_iter(cwd_path);
	
	if (opendir(cwd.c_str()) == nullptr) {
		cwd = "sdmc:/";
	}

	// int curr_file = 0;
	// zero index
	size_t selected_file = 0;
	

    bool update_files = true;
    std::vector<dirent> files;
	// Main loop
	while (aptMainLoop())
	{
		
		gspWaitForVBlank();
		gfxSwapBuffers();
		hidScanInput();
		
		
		// Your code goes here
		u32 kDown = hidKeysDown();
		if (kDown & KEY_START) {
			break; // break in order to return to hbmenu
		}

		// TODO test on 3ds how many files it takes to run out of memory (std::vector allocates on heap). Based on that decide if storing all files in cwd at once is viable or if smth different is needed 
		// std::vector<dirent> files = get_files(cwd.c_str());
        
        if (kDown) {
            update_files = true; // only update screen when a button is pressed
        }
        bool opus_error = false;
		// std::filesystem::path e(cwd);
		// A: enter directory TODO: play file
		if (kDown & KEY_A) {
			auto file_type = files[selected_file].d_type;
			if (file_type == DT_DIR) {
				cwd += files[selected_file].d_name;
				cwd += '/';
				selected_file = 0; // reset selected file to first file in new directory
			} else if (file_type == DT_REG) {
                if (opus_controller.songReady) {
                    // if song is already playing, stop playback
                    opus_controller.stopPlayback = true;
                    LightEvent_Wait(&opus_controller.doneEvent);
                }
                char* play_path = files[selected_file].d_name;
                PrintConsole *prev = consoleSelect(&bottomConsole);
				printf("Playing file: %s%s\n", cwd.c_str(), play_path);
                consoleSelect(prev);
                playSong(cwd + play_path);
			} 
			
		}
		// if (kDown & KEY_Y) {
		// 	// toggle playing
		// 	playing = !playing;
		// }

		if (kDown & KEY_B) {
            // if song is playing and user presses B, stop playback instead of going up a directory
            if (opus_controller.songReady) {
                opus_controller.stopPlayback = true;
                LightEvent_Wait(&opus_controller.doneEvent);
            } else {
                // TODO: maybe extract going up dir into a function START
                // ignore last character (trailing '/')
                size_t last_slash_idx = cwd.rfind('/', cwd.size()-2);
                // should always find a '/' since we prevent going above the root
                if (last_slash_idx != ROOT_SLASH_IDX && last_slash_idx != cwd.npos) {
                    // include slash
                    cwd = cwd.substr(0, last_slash_idx+1);
                    selected_file = 0; // reset selected file to first file in new directory
                }
                // maybe extract going up dir into a function END
            }
		}

		// DPad Up/Circle Pad Up: select previous file
		if (kDown & KEY_UP) {
			if (selected_file > 0) {
				selected_file--;
			} else {
				// wraparound TODO make it so holding up doesn't wraparound, only when tapping when first file selected
				selected_file = files.size() - 1; 
			}
		}
	
		// DPad Down/Circle Pad Down: select next file
		if (kDown & KEY_DOWN) {
			if (selected_file < files.size() - 1) {
				selected_file++;
			} else {
				selected_file = 0;
			}
		}
		
		// // DPad Left/Circle Pad Left: go down in tone
		// if (kDown & KEY_LEFT) {
		// 	if (note > 0) {
		// 		note--;
		// 	}
		// }
		
		// // DPad Right/Circle Pad Right: go up in tone
		// if (kDown & KEY_RIGHT) {
		// 	if ((size_t)note < sizeof(notefreq)/sizeof(notefreq[0]) - 1) {
		// 		note++;
		// 	}
		// }


		// if (playing && waveBuf[fillBlock].status == NDSP_WBUF_DONE) {
		// 	fill_buffer(waveBuf[fillBlock].data_pcm16, stream_offset, waveBuf[fillBlock].nsamples,notefreq[note]);

		// 	ndspChnWaveBufAdd(0, &waveBuf[fillBlock]);
		// 	stream_offset += waveBuf[fillBlock].nsamples;

		// 	fillBlock = !fillBlock;
		// }

		// printf("cwd: %s\n", cwd.c_str());
        if (update_files && !opus_error) {
            files = get_files(cwd.c_str());
         
            consoleClear();
            print_files(files, selected_file);
        }
        update_files = false; // reset update_files flag
		// list_files_in_dir(cwd);
        
	}

    // producer consumer design pattern START
    run_thread = false;
    // signal audio thread (it finishes since the flag is set to false)
    LightEvent_Signal(&opus_controller.startEvent);

    // Free the audio thread
    threadJoin(threadId, UINT64_MAX);
    threadFree(threadId);

    audioExit();
    ndspExit();

    romfsExit();
    gfxExit();    
    // producer consumer design pattern END


	// // from 3ds-examples/audio/opus-decoding START
	// // Signal audio thread to quit
    // s_quit = true;
    // LightEvent_Signal(&s_event);
	
    // // Cleanup audio things and de-init platform features
    // audioExit();
    // ndspExit();
    // op_free(opusFile);
	
    // romfsExit();
    // gfxExit();
	// // from 3ds-examples/audio/opus-decoding END

	
	return 0;
}
