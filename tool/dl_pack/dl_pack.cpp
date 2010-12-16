
#include <dl/dl.h>
#include <dl/dl_txt.h>
#include <dl/dl_convert.h>

#include <string.h>

#include "../../src/getopt/getopt.h" // these includes are horrific!
#include "../../src/container/dl_array.h"

#include <stdio.h>
#include <stdlib.h>

/*
	Tool that take SBDL-data in text-form and output a packed binary.
*/

int g_Verbose = 0;
int g_Unpack = 0;

void PrintHelp(SGetOptContext* _pCtx)
{
	char Buffer[2048];
	printf("usage: dl_pack.exe [options] file_to_pack\n\n");
	printf("%s", GetOptCreateHelpString(_pCtx, Buffer, 2048));
}

unsigned char* ReadFile(FILE* _pFile, unsigned int* Size)
{
	fseek(_pFile, 0, SEEK_END);
	*Size = ftell(_pFile);
	fseek(_pFile, 0, SEEK_SET);

	unsigned char* pData = (unsigned char*)malloc(*Size + 1);
	pData[*Size] = '\0';
	size_t read = fread(pData, 1, *Size, _pFile);
	(void)read;
	return pData;
}

#define M_VERBOSE_OUTPUT(fmt, ...) if(g_Verbose) { fprintf(stderr, fmt "\n", ##__VA_ARGS__); }

#define M_ERROR_AND_FAIL(fmt, ...) { fprintf(stderr, "Error: " fmt "\n", ##__VA_ARGS__); return 0x0; }

dl_ctx_t CreateContext(CArrayStatic<const char*, 128>& _lLibPaths, CArrayStatic<const char*, 128>& _lLibs)
{
	dl_ctx_t Ctx;
	dl_create_params_t p;
	DL_CREATE_PARAMS_SET_DEFAULT(p);
	dl_error_t err = dl_context_create( &Ctx, &p );
	if(err != DL_ERROR_OK)
		M_ERROR_AND_FAIL( "SBDL error while creating context: %s", dl_error_to_string(err));

	// load all type-libs.
	for(unsigned int iLib = 0; iLib < _lLibs.Len(); iLib++)
	{
		// search for lib!
		for (unsigned int iPath = 0; iPath < _lLibPaths.Len(); ++iPath)
		{
			// build testpath.
			char Path[2048];
			size_t PathLen = strlen(_lLibPaths[iPath]);
			strcpy(Path, _lLibPaths[iPath]);
			if(PathLen != 0 && Path[PathLen - 1] != '/')
				Path[PathLen++] = '/';
			strcpy(Path + PathLen, _lLibs[iLib]);

			FILE* File = fopen(Path, "rb");

			if(File != 0x0)
			{
				M_VERBOSE_OUTPUT("Reading type-library from file %s", Path);

				unsigned int Size;
				unsigned char* TL = ReadFile(File, &Size);
				err = dl_context_load_type_library(Ctx, TL, Size);
				if(err != DL_ERROR_OK)
					M_ERROR_AND_FAIL( "SBDL error while loading type library (%s): %s", Path, dl_error_to_string(err));

				free(TL);
				fclose(File);
				break;
			}
		}
	}

	return Ctx;
}

#define M_ERROR_AND_QUIT(fmt, ...) { fprintf(stderr, "Error: " fmt "\n", ##__VA_ARGS__); return 1; }

int main(int argc, char** argv)
{
	static const SOption OptionList[] =
	{
		{"help",    'h', E_OPTION_TYPE_NO_ARG,   0x0,        'h', "displays this help-message"},
		{"libpath", 'L', E_OPTION_TYPE_REQUIRED, 0x0,        'L', "add type-library include path", "path"},
		{"lib",     'l', E_OPTION_TYPE_REQUIRED, 0x0,        'l', "add type-library", "path"},
		{"output",  'o', E_OPTION_TYPE_REQUIRED, 0x0,        'o', "output to file", "file"},
		{"endian",  'e', E_OPTION_TYPE_REQUIRED, 0x0,        'e', "endianness of output data, if not specified pack-platform is assumed", "little,big"},
		{"ptrsize", 'p', E_OPTION_TYPE_REQUIRED, 0x0,        'p', "ptr-size of output data, if not specified pack-platform is assumed", "4,8"},
		{"unpack",  'u', E_OPTION_TYPE_FLAG_SET, &g_Unpack,    1, "force dl_pack to treat input data as a packed instance that should be unpacked."},
		{"verbose", 'v', E_OPTION_TYPE_FLAG_SET, &g_Verbose,   1, "verbose output"},
		{0}
	};

	SGetOptContext GOCtx;
	GetOptCreateContext(&GOCtx, argc, argv, OptionList);

	CArrayStatic<const char*, 128> lLibPaths; lLibPaths.Add("");
	CArrayStatic<const char*, 128> lLibs;
	const char*  pOutput  = "";
	const char*  pInput   = "";
	dl_endian_t   Endian  = DL_ENDIAN_HOST;
	unsigned int PtrSize = sizeof(void*);

	int32 opt;
	while((opt = GetOpt(&GOCtx)) != -1)
	{
		switch(opt)
		{
			case 'h': PrintHelp(&GOCtx); return 0;
			case 'L':
				if(lLibPaths.Full())
					M_ERROR_AND_QUIT("dl_pack only supports %u libpaths!", (unsigned int)lLibPaths.Capacity());

				lLibPaths.Add(GOCtx.m_CurrentOptArg);
				break;
			case 'l':
				if(lLibs.Full())
					M_ERROR_AND_QUIT("dl_pack only supports %u type libraries libs!", (unsigned int)lLibs.Capacity());

				lLibs.Add(GOCtx.m_CurrentOptArg);
				break;
			case 'o':
				if(pOutput[0] != '\0')
					M_ERROR_AND_QUIT("output-file already set to: \"%s\", trying to set it to \"%s\"", pOutput, GOCtx.m_CurrentOptArg);

				pOutput = GOCtx.m_CurrentOptArg;
				break;
			case 'e':
				if(strcmp(GOCtx.m_CurrentOptArg, "little") == 0)
					Endian = DL_ENDIAN_LITTLE;
				else if(strcmp(GOCtx.m_CurrentOptArg, "big") == 0) 
					Endian = DL_ENDIAN_BIG;
				else
					M_ERROR_AND_QUIT("endian-flag need \"little\" or \"big\", not \"%s\"!", GOCtx.m_CurrentOptArg);
				break;
			case 'p':
				if(strlen(GOCtx.m_CurrentOptArg) != 1 || (GOCtx.m_CurrentOptArg[0] != '4' && GOCtx.m_CurrentOptArg[0] != '8'))
					M_ERROR_AND_QUIT("ptr-flag need \"4\" or \"8\", not \"%s\"!", GOCtx.m_CurrentOptArg);

				PtrSize = GOCtx.m_CurrentOptArg[0] - '0';
				break;
			case '!': M_ERROR_AND_QUIT("incorrect usage of flag \"%s\"!", GOCtx.m_CurrentOptArg);
			case '?': M_ERROR_AND_QUIT("unrecognized flag \"%s\"!", GOCtx.m_CurrentOptArg);
			case '+':
				if(pInput[0] != '\0')
					M_ERROR_AND_QUIT("input-file already set to: \"%s\", trying to set it to \"%s\"", pInput, GOCtx.m_CurrentOptArg);

				pInput = GOCtx.m_CurrentOptArg;
				break;
			case 0: break; // ignore, flag was set!
			default:
				DL_ASSERT(false && "This should not happen!");
		}
	}

	FILE* pInFile  = stdin;
	FILE* pOutFile = stdout;

	// TODO: Support read stdin, write stdout
	if(pInput[0] == '\0')
		M_ERROR_AND_QUIT("Should have read input from stdin, but this is not supported yet =/");

	pInFile = fopen(pInput, "rb");
	if(pInFile == 0x0) 
		M_ERROR_AND_QUIT("Could not open input file: %s", pInput);

	if(pOutput[0] != '\0')
	{
		pOutFile = fopen(pOutput, "wb");
		if(pOutFile == 0x0) 
			M_ERROR_AND_QUIT("Could not open output file: %s", pOutput);
	}

	unsigned int Size;
	unsigned char* InData = ReadFile(pInFile, &Size);

	dl_ctx_t Ctx = CreateContext(lLibPaths, lLibs);
	if(Ctx == 0x0)
		return 1;

	unsigned char* pOutData = 0x0;
	unsigned int   OutDataSize = 0;

	if(g_Unpack == 1) // should unpack
	{
		dl_instance_info_t info;
		dl_instance_get_info( InData, Size, &info );
		if(sizeof(void*) <= info.ptrsize)
		{
			// we are converting ptr-size down and can use the faster inplace load.
			dl_error_t err = dl_convert_inplace(Ctx, info.root_type, InData, Size, 0x0, DL_ENDIAN_HOST, sizeof(void*));
			if(err != DL_ERROR_OK)
				M_ERROR_AND_QUIT( "SBDL error converting packed instance: %s", dl_error_to_string(err));
		}
		else
		{
			// instance might grow so ptr-data so the non-inplace unpack is needed.
			unsigned int SizeAfterConvert;
			dl_error_t err = dl_convert_calc_size(Ctx, info.root_type, InData, Size, PtrSize, &SizeAfterConvert);

			if(err != DL_ERROR_OK)
				M_ERROR_AND_QUIT( "SBDL error converting endian of data: %s", dl_error_to_string(err));

			unsigned char* pConverted = (unsigned char*)malloc(SizeAfterConvert);

			err = dl_convert(Ctx, info.root_type, InData, Size, pConverted, SizeAfterConvert, DL_ENDIAN_HOST, sizeof(void*));
			if(err != DL_ERROR_OK)
				M_ERROR_AND_QUIT( "SBDL error converting endian of data: %s", dl_error_to_string(err));

			free(InData);
			InData = pConverted;
			Size   = SizeAfterConvert;
		}

		dl_error_t err = dl_txt_unpack_calc_size(Ctx, info.root_type, InData, Size, &OutDataSize);
		if(err != DL_ERROR_OK)
			M_ERROR_AND_QUIT( "SBDL error while calculating unpack size: %s", dl_error_to_string(err));

		pOutData = (unsigned char*)malloc(OutDataSize);

		err = dl_txt_unpack(Ctx, info.root_type, InData, Size, (char*)pOutData, OutDataSize);
		if(err != DL_ERROR_OK)
			M_ERROR_AND_QUIT( "SBDL error while unpacking: %s", dl_error_to_string(err));
	}
	else
	{
		dl_error_t err = dl_txt_pack_calc_size(Ctx, (char*)InData, &OutDataSize);
		if(err != DL_ERROR_OK)
			M_ERROR_AND_QUIT("SBDL error while calculating pack size: %s", dl_error_to_string(err));

		pOutData = (unsigned char*)malloc(OutDataSize);

		err = dl_txt_pack(Ctx, (char*)InData, pOutData, OutDataSize);
		if(err != DL_ERROR_OK)
			M_ERROR_AND_QUIT("SBDL error while packing: %s", dl_error_to_string(err));

		dl_instance_info_t info;
		dl_instance_get_info( pOutData, OutDataSize, &info);

		err = dl_convert_inplace(Ctx, info.root_type, pOutData, OutDataSize, 0x0, Endian, PtrSize);
		if(err != DL_ERROR_OK)
			M_ERROR_AND_QUIT("SBDL error while converting packed instance: %s", dl_error_to_string(err));
	}

	fwrite(pOutData, 1, OutDataSize, pOutFile);
	free(pOutData);
	free(InData);

	if(pInput[0] != '\0')  fclose(pInFile);
	if(pOutput[0] != '\0') fclose(pOutFile);

	return 0;
}
