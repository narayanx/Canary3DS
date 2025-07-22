#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem>
#include <dirent.h>

#include <3ds.h>
#include <citro2d.h>
#include <string>
// #include <array>
#include <vector>

		
// from 3ds-examples/audio/streaming START
#define SAMPLERATE 22050
#define SAMPLESPERBUF (SAMPLERATE / 30)
#define BYTESPERSAMPLE 4
// from 3ds-examples/audio/streaming END

// not designed to work with values other than 2 buffers, but that is not necessary
const int NUM_BUFFERS = 2;

const int MAX_PATH_CHAR_LENGTH = 4096; // max file name seems to be 255, file paths are concatenated filenames
const int MAX_FILES = 16; // max files to display at once

void exit_exception() {
	while (aptMainLoop()) {
		gspWaitForVBlank();
		gfxSwapBuffers();
		hidScanInput();
		u32 kDown = hidKeysDown();
		if (kDown & KEY_START)
			return;
	}
}


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

void print_files(std::vector<dirent> files, size_t selectedFile) {
	for (size_t i = 0; i < files.size(); i++) {
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

// from 3ds-examples/audio/streaming START

void fill_buffer(void *audioBuffer,size_t offset, size_t size, int frequency ) {
	u32 *dest = (u32*)audioBuffer;
	
	for (size_t i=0; i<size; i++) {
		
		s16 sample = INT16_MAX * sin(frequency*(2*M_PI)*(offset+i)/SAMPLERATE);
		
		dest[i] = (sample<<16) | (sample & 0xffff);
	}
	
	DSP_FlushDataCache(audioBuffer,size);
}
// from 3ds-examples/audio/streaming END


int main(int argc, char* argv[])
{
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);
	// alternate between two buffers to allow for filling one while the other is playing
	ndspWaveBuf waveBuf[NUM_BUFFERS];
	
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
	// two buffers
	u32 *audioBuffer = (u32*)linearAlloc(SAMPLESPERBUF*BYTESPERSAMPLE*NUM_BUFFERS);
	// only 2 buffers so can use bool instead of int
	bool fillBlock = false;
	
	// TODO add a msg telling ppl how to dump with luma3ds (likely bc dspfirm isn't dumped)
	ndspInit();

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, SAMPLERATE);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
	
	float mix[12];
	memset(mix, 0, sizeof(mix));
	mix[0] = 1.0;
	mix[1] = 1.0;
	ndspChnSetMix(0, mix);

	// c4-c6
	int notefreq[] = {
		262, 294, 329, 349, 392, 440, 494, 523, 587, 659, 698, 784, 880, 988, 1046
	};

	int note = 0;

	memset(waveBuf,0,sizeof(waveBuf));
	waveBuf[0].data_vaddr = &audioBuffer[0];
	waveBuf[0].nsamples = SAMPLESPERBUF;
	waveBuf[1].data_vaddr = &audioBuffer[SAMPLESPERBUF];
	waveBuf[1].nsamples = SAMPLESPERBUF;

	size_t stream_offset = 0;

	fill_buffer(audioBuffer,stream_offset, SAMPLESPERBUF * 2, notefreq[note]);
	stream_offset += SAMPLESPERBUF;

	ndspChnWaveBufAdd(0, &waveBuf[0]);
	ndspChnWaveBufAdd(0, &waveBuf[1]);
	

	bool playing = false;
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
		std::vector<dirent> files = get_files(cwd.c_str());
		// std::filesystem::path e(cwd);

		// A: enter directory TODO: play file
		if (kDown & KEY_A) {
			auto file_type = files[selected_file].d_type;
			if (file_type == DT_DIR) {
				cwd += files[selected_file].d_name;
				cwd += '/';
				selected_file = 0; // reset selected file to first file in new directory
			} else if (file_type == DT_REG) {
				// TODO play file
				printf("Playing file: %s%s\n", cwd.c_str(), files[selected_file].d_name);
				
			} 
			
		}
		if (kDown & KEY_Y) {
			// toggle playing
			playing = !playing;
		}

		if (kDown & KEY_B) {
			// ignore last character (trailing '/')
			size_t last_slash_idx = cwd.rfind('/', cwd.size()-2);
			// should always find a '/' since we prevent going above the root
			if (last_slash_idx != ROOT_SLASH_IDX && last_slash_idx != cwd.npos) {
				// include slash
				cwd = cwd.substr(0, last_slash_idx+1);
				selected_file = 0; // reset selected file to first file in new directory
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
		
		// DPad Left/Circle Pad Left: go down in tone
		if (kDown & KEY_LEFT) {
			if (note > 0) {
				note--;
			}
		}
		
		// DPad Right/Circle Pad Right: go down in tone
		if (kDown & KEY_RIGHT) {
			if ((size_t)note < sizeof(notefreq)/sizeof(notefreq[0]) - 1) {
				note++;
			}
		}


		if (playing && waveBuf[fillBlock].status == NDSP_WBUF_DONE) {
			fill_buffer(waveBuf[fillBlock].data_pcm16, stream_offset, waveBuf[fillBlock].nsamples,notefreq[note]);

			ndspChnWaveBufAdd(0, &waveBuf[fillBlock]);
			stream_offset += waveBuf[fillBlock].nsamples;

			fillBlock = !fillBlock;
		}

		consoleClear();
		// printf("cwd: %s\n", cwd.c_str());
		print_files(files, selected_file);
		// list_files_in_dir(cwd);
	}

	ndspExit();
	linearFree(audioBuffer);
	
	gfxExit();
	return 0;
}
