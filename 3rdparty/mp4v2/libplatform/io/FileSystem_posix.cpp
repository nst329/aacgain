#include "libplatform/impl.h"
#include <sys/stat.h>

namespace mp4v2 { namespace platform { namespace io {

///////////////////////////////////////////////////////////////////////////////

bool
FileSystem::exists( string path_ )
{
    struct stat buf;
    return stat( path_.c_str(), &buf ) == 0;
}

///////////////////////////////////////////////////////////////////////////////

bool
FileSystem::isDirectory( string path_ )
{
    struct stat buf;
    if( stat( path_.c_str(), &buf ))
        return false;
    return S_ISDIR( buf.st_mode );
}

///////////////////////////////////////////////////////////////////////////////

bool
FileSystem::isFile( string path_ )
{
    struct stat buf;
    if( stat( path_.c_str(), &buf ))
        return false;
    return S_ISREG( buf.st_mode );
}

///////////////////////////////////////////////////////////////////////////////

bool
FileSystem::getFileSize( string path_, File::Size& size_ )
{
    size_ = 0;
    struct stat buf;
    if( stat( path_.c_str(), &buf ))
        return true;
    size_ = buf.st_size;
    return false;
}

///////////////////////////////////////////////////////////////////////////////

bool
FileSystem::rename( string from, string to )
{
//    return ::rename( from.c_str(), to.c_str() ) != 0;

    const char *oldFileName = from.c_str();
    const char *newFileName = to.c_str();
    int rc = ::rename( from.c_str(), to.c_str() ) ;
	if( rc < 0 )
	{
	    ::remove(newFileName) ;

	    FILE *inFile = ::fopen(oldFileName, "rb");
	    if (!inFile)
		return false;

	    FILE *outFile = ::fopen(newFileName, "wb");
	    if (!outFile)
		return false;


	    //copy the original file to the temp file
	    static const u_int32_t blockSize = (1024*1024);
	    u_int8_t *buffer = new u_int8_t[blockSize];
	    for (;;)
	    {
		int bytesRead = ::fread(buffer, 1, blockSize, inFile);
		if (bytesRead)
		    ::fwrite(buffer, 1, bytesRead, outFile);
		if (bytesRead < blockSize)
		    break;
	    }
	    ::fclose(inFile);
	    ::fclose(outFile);
	    ::remove(oldFileName);
	    rc = 0 ;
	}
	return rc != 0;

}

///////////////////////////////////////////////////////////////////////////////

string FileSystem::DIR_SEPARATOR  = "/";
string FileSystem::PATH_SEPARATOR = ":";

///////////////////////////////////////////////////////////////////////////////

}}} // namespace mp4v2::platform::io
