/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

import java.io.*;


// Class corresponds to CLOG_Rec_CommEvt
public class RecComm
{
    public  static final int RECTYPE  = Const.RecType.COMMEVT;
    private static final int BYTESIZE = 4 * 4;
    public         Integer   etype;            // type of communicator creation
    public         int       parent;           // parent communicator
    public         int       newcomm;          // new communicator
    private static int       pad;              // byte padding

    public int readFromDataStream( DataInputStream in )
    {
        try {
            etype     = new Integer( in.readInt() );
            parent    = in.readInt();
            newcomm   = in.readInt();
            pad       = in.readInt();
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            return 0;
        }

        return BYTESIZE;
    }

    public int skipBytesFromDataStream( DataInputStream in )
    {
        try {
            in.skipBytes( BYTESIZE );
        } catch ( IOException ioerr ) {
            ioerr.printStackTrace();
            return 0;
        }

        return BYTESIZE;
    }

    public String toString()
    {
        return ( "RecComm"
               + "[ etype=" + etype
               + ", parent=" + parent
               + ", newcomm=" +  newcomm
               // + ", pad=" + pad
               // + ", BYTESIZE=" + BYTESIZE
               + " ]");
    }
}  
