#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>

#include <freetype2/ft2build.h>
#include FT_FREETYPE_H
#include FT_FONT_FORMATS_H

#include <aosbf.h>

#define BDFCONV_VERSION "vDEV0.1"
#define BDFCONV_NAME "bdfconv"

static const char* HELP_STR =
    "Usage: bdfconv [OPTIONS]\n"
    "\n"
    "Options:\n"
    "\t-v, --version        Print version information\n"
    "\t-h, --help           Print this help message\n"
    "\t-t, --type <type>    Output type. Supported:\n"
	"\t\t\tAOSBF (default)"
    "\t-o, --output <file>  Output file\n"
    "\t-i, --input <file>   Input BDF file\n";

typedef enum {
    OUTPUT_AOSBF
} output_type;

typedef struct {
    const char* input;
    const char* output;
    output_type type;
} arg_state;

static bool parse_args(int argc, char** argv, arg_state* out) {
    static const struct option long_options[] = {
        { "version", no_argument, NULL, 'v' },
        { "help", no_argument, NULL, 'h' },
        { "type", required_argument, NULL, 't' },
        { "output", required_argument, NULL, 'o' },
        { "input", required_argument, NULL, 'i' },
        { NULL, 0, NULL,  0  }
    };

    int option;
    if (out == NULL) return false;

    out->input  = NULL;
    out->output = NULL;
    out->type = OUTPUT_AOSBF;

    while ((option = getopt_long(argc, argv, "vht:o:i:", long_options, NULL)) != -1) {
        switch (option) {
			case 'v': {
				printf("%s %s\n", BDFCONV_NAME, BDFCONV_VERSION);
				break;
			}

			case 'h': {
				printf(HELP_STR);
				break;
			}

			case 't': {
				if (!optarg) {
					fprintf(stderr, "Error: (NULL) value for '--type' Recieved!\n");
					return false;
				}

				if (strcmp(optarg, "AOSBF") == 0) out->type = OUTPUT_AOSBF;
				else {
					fprintf(stderr, "Error: value '%s' for '--type' is not supported!\n", optarg);
					return false;
				}
				break;
			}

			case 'o': {
				out->output = optarg;
				break;
			}

			case 'i': {
				out->input = optarg;
				break;
			}

			case '?':
			default: {
				fprintf(stderr, "Error: Unknown Option: %s\n", optarg ? optarg : "(NULL)");
				return false;
			}
        }
    }

    return true;
}

static bool load_bdf(const char* path, FT_Library* lib_out, FT_Face* face_out) {
	FT_Library library = NULL;
    FT_Face face = NULL;

    FT_Error error;

    error = FT_Init_FreeType(&library);
    if (error) {
        fprintf(stderr, "Error: Could not initialize FreeType: %d\n", error);
        return false;
    }

    error = FT_New_Face(library, path, 0, &face);
    if (error) {
        fprintf(stderr, "Error: '%s' is not a BDF font: %d\n", path, error);
        FT_Done_FreeType(library);
        return false;
    }

	const char* format = FT_Get_Font_Format(face);
	if (!format || strcmp(format, "BDF") != 0) {
		fprintf(stderr, "Error: '%s' is not a BDF font (detected format: %s)\n", path, format ? format : "(unknown)");

		FT_Done_Face(face);
		FT_Done_FreeType(library);

		return false;
	}

	if (lib_out) *lib_out = library;
	if (face_out) *face_out = face;

    return true;
}

int main(int argc, char** argv) {
	arg_state args = {0};
	if (!parse_args(argc, argv, &args)) return -1;

	if (!args.input) {
		fprintf(stderr, "Error: No Input file specified (use '--input' to specify input file)\n");
		return -1;
	} else if (!args.output) {
		fprintf(stderr, "Error: No Output file specified (use '--output' to specify output file)\n");
		return -1;
	}

	FT_Library lib = {0};
	FT_Face face = {0};
	if (!load_bdf(args.input, &lib, &face)) return -1;

	bool error = false;

	switch (args.type) {
		case OUTPUT_AOSBF: {
			error = !(build_aosbf(args.output, face));
			break;
		}

		default: {
			fprintf(stderr, "Error: Unsupported value for '--type'\n");
			error = true;
			break;
		}
	}

	FT_Done_Face(face);
    FT_Done_FreeType(lib);

	return error ? -1 : 0;
}
