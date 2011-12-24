/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clog2;

public class Const
{
    // Current Version ID
           static final String    VERSION         = "CLOG-02.20";

    // Older Version IDs, COMPAT_VERSIONS, that are compataible to VERSION.
    // Version 02.20 is compatible with 02.10.
    // If No compatibel version, define COMPAT_VERSIONS as follows
    //       static final String[]  COMPAT_VERSIONS = {};
           static final String[]  COMPAT_VERSIONS = { "CLOG-02.10" };

           static final byte      INVALID_byte    = Byte.MIN_VALUE;
           static final short     INVALID_short   = Short.MIN_VALUE;
           static final int       INVALID_int     = Integer.MIN_VALUE;
           static final long      INVALID_long    = Long.MIN_VALUE;
           static final int       NULL_int        = 0;
           static final int       NULL_iaddr      = 0;
           static final long      NULL_fptr       = 0;
           static final float     INVALID_float   = Float.MIN_VALUE;
           static final double    INVALID_double  = Double.MIN_VALUE;
           static final int       TRUE            = 1;
           static final int       FALSE           = 0;

    //  Define constants listed in <MPE2>/src/logging/include/clog_record.h

    //  MPE_MAX_KNOWN_STATES is the MPE's max. MPI system states.
    //  Let's be generous, instead of defining MPE_MAX_KNOWN_STATES = 128,
    //  define MPE_MAX_KNOWN_STATES = 180 to include MPI-IO routines
    //  define MPE_MAX_KNOWN_STATES = 200 to include MPI-RMA routines

    //  MPE_MAX_KNOWN_STATE defined to be number of stateID in the range of
    //  [CLOG_KNOWN_STATEID_START .... CLOG_USER_STATEID_START - 1]
    //  in logging/include/clog.h.  The MPE_MAX_KNOWN_STATES is larger than
    //  the C version of MPE_MAX_KNOWN_STATES defined in
    //  wrappers/src/log_mpi_core.c, so that the Java code does not need
    //  to be updated too often.

           static final int       MPE_MAX_KNOWN_STATES = 300;

    // !arbitary! defined for max. user-defined states. Increase it if necessary

           static final int       MPE_MAX_USER_STATES  = 100;

    // variable "eventID" is used in MPI_Init()
    // of <MPE>/src/logging/src/log_mpi_core.c

           static final int       MPE_1ST_KNOWN_EVENT  = 0;  // i.e. eventID

    //  The initial event_number return by MPE_Log_get_event_number();
    //  Ref: clog.h, mpe_log.c

           static final int       MPE_1ST_USER_EVENT  = 600;  // CLOG_MAXEVENT

    public class AllType
    {
        public static final int   UNDEF     =  -1;         // CLOG_REC_UNDEF
    }

    public class RecType
    {
        // public static final int   UNDEF     =  -1;        // CLOG_REC_UNDEF
        public static final int   ENDLOG    =  0;         // CLOG_REC_ENDLOG
        public static final int   ENDBLOCK  =  1;         // CLOG_REC_ENDBLOCK
               static final int   STATEDEF  =  2;         // CLOG_REC_STATEDEF
               static final int   EVENTDEF  =  3;         // CLOG_REC_EVENTDEF
               static final int   CONSTDEF  =  4;         // CLOG_REC_CONSTDEF
               static final int   BAREEVT   =  5;         // CLOG_REC_BAREEVT
               static final int   CARGOEVT  =  6;         // CLOG_REC_CARGOEVT
               static final int   MSGEVT    =  7;         // CLOG_REC_MSGEVT
               static final int   COLLEVT   =  8;         // CLOG_REC_COLLEVT
               static final int   COMMEVT   =  9;         // CLOG_REC_COMMEVT
               static final int   SRCLOC    = 10;         // CLOG_REC_SRCLOC
               static final int   SHIFT     = 11;         // CLOG_REC_TIMESHIFT
    }

    class MsgType // is used to define send and recv events
    {
               static final int   SEND      = -101;       // CLOG_EVT_SENDMSG
               static final int   RECV      = -102;       // CLOG_EVT_RECVMSG
    }
}
