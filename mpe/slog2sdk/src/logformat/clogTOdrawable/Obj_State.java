/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package logformat.clogTOdrawable;

import base.drawable.Coord;
import base.drawable.Category;
// import base.drawable.Primitive;
import base.drawable.Primitive_TwoVtx;

public class Obj_State extends Primitive_TwoVtx // which extends Primitive
{
    public Obj_State()
    {
        super();
    }

    public Obj_State( final Category obj_type )
    {
        super( obj_type );
    }

    public Obj_State( final Category obj_type,
                      final Coord  start_vtx, final Coord  final_vtx )
    {
        super( obj_type, start_vtx, final_vtx );
    }

    public String toString()
    {
        return ( "State{ " + super.toString() + " }" );
    }
}
