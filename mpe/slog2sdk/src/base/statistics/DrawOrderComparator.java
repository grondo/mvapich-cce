/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.statistics;

import java.util.Comparator;

import base.drawable.TimeBoundingBox;

/*
    This comparator to Collections.sort() will make Drawables be arranged
    in increasing starttime order.  If starttimes are equals, Drawables will
    then be arranged in decreasing finaltime order.

    Since the comparator is used in the TreeMap of class
    logformat/slog2/input/TreeFloorList.ForeItrOfDrawables
    where the likihood of equal starttime is high.  In order to avoid
    over-written of iterators due to false equivalent drawables,
    i.e. using starttime and endtime may not be enough.  We implement
    a very strict form of comparator for drawable for drawing order.
*/
public class DrawOrderComparator implements Comparator
{
    public int compare( Object o1, Object o2 )
    {
        TimeBoundingBox  timebox1, timebox2;
        timebox1 = (TimeBoundingBox) o1;
        timebox2 = (TimeBoundingBox) o2;
        double  timebox1_starttime, timebox2_starttime;
        timebox1_starttime = timebox1.getEarliestTime();
        timebox2_starttime = timebox2.getEarliestTime();
        if ( timebox1_starttime != timebox2_starttime )
            // increasing starttime order ( 1st order )
            return ( timebox1_starttime < timebox2_starttime ? -1 : 1 );
        else { 
            double  timebox1_finaltime, timebox2_finaltime;
            timebox1_finaltime = timebox1.getLatestTime();
            timebox2_finaltime = timebox2.getLatestTime();
            if ( timebox1_finaltime != timebox2_finaltime )
                // decreasing finaltime order ( 2nd order )
                return ( timebox1_finaltime > timebox2_finaltime ? -1 : 1 );
            else {
                // if ( timebox1 == timebox2 )
                    return 0;
            }   // FinalTime
        }   // StartTime
    }
}
