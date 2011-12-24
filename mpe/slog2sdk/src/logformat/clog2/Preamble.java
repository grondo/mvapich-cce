/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

import java.util.StringTokenizer;
import java.util.NoSuchElementException;
import java.io.DataInputStream;
import java.io.IOException;

// Class corresponds to CLOG_Premable
public class Preamble
{
    // BYTESIZE corresponds to CLOG_PREAMBLE_SIZE
    private final static int      BYTESIZE     = 1024;
    // VERSIONSIZE corresponds to CLOG_VERSION_STRLEN;
    private final static int      VERSIONSIZE  = 12;

    private              String   version;
    //  num_blocks correspond to CLOG_Premable_t.is_big_endian
    private              String   is_big_endian_title;
    private              boolean  is_big_endian;
    //  num_blocks correspond to CLOG_Premable_t.block_size
    private              String   block_size_title;
    private              int      block_size;
    //  num_blocks correspond to CLOG_Premable_t.num_buffered_blocks
    private              String   num_blocks_title;
    private              int      num_blocks;

    public boolean readFromDataStream( DataInputStream in )
    {
        byte[]           buffer;
        StringTokenizer  tokens;
        String           str;

        buffer = new byte[ BYTESIZE ];
        try {
            in.readFully( buffer );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            return false;
        }

        tokens  = new StringTokenizer( new String( buffer ), "\0" );
        try {
            version = tokens.nextToken().trim();
            is_big_endian_title = tokens.nextToken().trim();
            str                 = tokens.nextToken().trim();
            is_big_endian       =  str.equalsIgnoreCase( "true" )
                                || str.equalsIgnoreCase( "yes" );
            block_size_title    = tokens.nextToken().trim();
            block_size          = Integer.parseInt( tokens.nextToken().trim() );
            num_blocks_title    = tokens.nextToken().trim();
            num_blocks          = Integer.parseInt( tokens.nextToken().trim() );
        } catch ( NoSuchElementException err ) {
            err.printStackTrace();
            return false;
        } catch ( NumberFormatException err ) {
            err.printStackTrace();
            return false;
        }

        return true;
    }

    public String  getVersionString()
    {
        return version;
    }

    public boolean isVersionMatched()
    {
        return version.equalsIgnoreCase( Const.VERSION );
    }

    public boolean isVersionCompatible()
    {
        String   old_version;
        boolean  isCompatible;
        int      idx;

        isCompatible = false;
        for ( idx = 0;
              idx < Const.COMPAT_VERSIONS.length && !isCompatible;
              idx++ ) {
            old_version   = Const.COMPAT_VERSIONS[ idx ];
            isCompatible  = version.equalsIgnoreCase( old_version );
        }
        return isCompatible;
    }

    public boolean isBigEndian()
    {
        return is_big_endian;
    }

    public int getBlockSize()
    {
        return block_size;
    }

    public String toString()
    {
         return ( version + "\n"
                + is_big_endian_title + is_big_endian + "\n"
                + block_size_title + block_size + "\n"
                + num_blocks_title + num_blocks + "\n" );
    }
}
