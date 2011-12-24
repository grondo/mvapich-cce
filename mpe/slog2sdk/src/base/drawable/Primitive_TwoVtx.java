/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

public class Primitive_TwoVtx extends Primitive
{
    public Primitive_TwoVtx()
    {
        super( 2 );
    }

    public Primitive_TwoVtx( final Category obj_type )
    {
        super( obj_type, 2 );
    }

    public Primitive_TwoVtx( final Category obj_type,
                             final Coord  start_vtx, final Coord  final_vtx )
    {
        super( obj_type, 2 );
        super.setStartVertex( start_vtx );
        super.setFinalVertex( final_vtx );
    }

    public Primitive_TwoVtx( final Category  obj_type,
                             final Coord[]   in_vertices )
    throws IllegalArgumentException
    {
        super( obj_type, 2 );
        if ( in_vertices.length != 2 )
            throw new IllegalArgumentException( "Input vertices length == "
                                              + in_vertices.length + " != 2." );
        else {
            super.setStartVertex( in_vertices[ 0 ] );
            super.setFinalVertex( in_vertices[ 1 ] );
        }    
    }
}
