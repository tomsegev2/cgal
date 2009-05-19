// Copyright (c) 2007-09  INRIA Sophia-Antipolis (France).
// All rights reserved.
//
// This file is part of CGAL (www.cgal.org); you may redistribute it under
// the terms of the Q Public License version 1.0.
// See the file LICENSE.QPL distributed with CGAL.
//
// Licensees holding a valid commercial license may use this file in
// accordance with the commercial license agreement provided with the software.
//
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
//
// $URL$
// $Id$
//
// Author(s) : Laurent Saboret and Andreas Fabri

#ifndef CGAL_MST_ORIENT_NORMALS_H
#define CGAL_MST_ORIENT_NORMALS_H

#include <CGAL/Search_traits_3.h>
#include <CGAL/Orthogonal_k_neighbor_search.h>
#include <CGAL/Search_traits_vertex_handle_3.h>
#include <CGAL/point_set_property_map.h>
#include <CGAL/Memory_sizer.h>
#include <CGAL/point_set_processing_assertions.h>

#include <iterator>
#include <list>
#include <climits>
#include <math.h>
#ifndef M_PI
  #define M_PI       3.14159265358979323846
#endif

#include <boost/property_map.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/prim_minimum_spanning_tree.hpp>

CGAL_BEGIN_NAMESPACE


// ----------------------------------------------------------------------------
// Private section
// ----------------------------------------------------------------------------
namespace CGALi {


/// Generalization of std::distance() to compute the distance between 2 integers
inline int
distance(std::size_t _First, std::size_t _Last)
{
  // return int difference
  return _Last - _First;
}


/// Functor for operator< that compares iterators address.
template <typename Iterator>
struct Compare_iterator_address
{
  bool operator()(const Iterator& lhs, const Iterator& rhs) const
  {
    return (&*lhs < &*rhs);
  }
};


/// Helper class: Riemannian graph.
///
/// This class is used internally by mst_orient_normals()
/// to encode:
/// - the adjacency relations of vertices in a K-neighboring.
/// - vertices contain the corresponding input point iterator.
/// - the edge weight = edge weight = 1 - | normal1 * normal2 |
///   where normal1 and normal2 are the normal at the edge extremities.

template <typename ForwardIterator> ///< Input point iterator
struct Riemannian_graph_vertex_properties {
    ForwardIterator input_point; ///< Input point
};
template <typename ForwardIterator> ///< Input point iterator
class Riemannian_graph
  : public boost::adjacency_list< boost::vecS, boost::vecS,
                                  boost::undirectedS,
                                  Riemannian_graph_vertex_properties<ForwardIterator>,
                                  boost::property<boost::edge_weight_t, float> >
{
};


/// Helper class: MST graph
///
/// This class is used internally by mst_orient_normals()
/// to encode:
/// - the adjacency relations of vertices in a Minimum Spanning Tree.
/// - vertices contain the corresponding input point iterator
//    and a boolean indicating if the normal is oriented.

template <typename ForwardIterator> ///< Input point iterator
struct MST_graph_vertex_properties {
    ForwardIterator input_point; ///< Input point
    bool is_oriented; ///< Is input point's normal oriented?
};
template <typename ForwardIterator, ///< Input point iterator
          typename NormalPMap, ///< property map ForwardIterator -> Normal
          typename Kernel ///< Geometric traits class
>
class MST_graph
  : public boost::adjacency_list< boost::vecS, boost::vecS,
                                  boost::directedS,
                                  MST_graph_vertex_properties<ForwardIterator> >
{
public:
    MST_graph(NormalPMap normal_pmap) : m_normal_pmap(normal_pmap) {}

// Public data
    const NormalPMap m_normal_pmap;
};


/// Helper class: Propagate_normal_orientation
///
/// This class is used internally by mst_orient_normals()
/// to propage the normal orientation, starting from a source point
/// and following the adjacency relations of vertices in a Minimum Spanning Tree.
/// It does not orient normals that are already oriented.
/// It does not propagate the orientation if the angle between 2 normals > angle_max.
///
/// @commentheading Preconditions:
/// - Normals must be unit vectors.
/// - 0 < angle_max <= PI/2.
///
/// @commentheading Template Parameters:
/// @param ForwardIterator iterator over input points.
/// @param NormalPMap is a model of boost::ReadWritePropertyMap.
/// @param Kernel Geometric traits class.

template <typename ForwardIterator, ///< Input point iterator
          typename NormalPMap, ///< property map ForwardIterator -> Normal
          typename Kernel
>
struct Propagate_normal_orientation
  : public boost::base_visitor< Propagate_normal_orientation<ForwardIterator, NormalPMap, Kernel> >
{
    typedef CGALi::MST_graph<ForwardIterator, NormalPMap, Kernel> MST_graph;
    typedef boost::on_examine_edge event_filter;

    Propagate_normal_orientation(double angle_max = M_PI/2.) ///< max angle to propagate the normal orientation (radians)
    : m_angle_max(angle_max)
    {
        // Precondition: 0 < angle_max <= PI/2
        CGAL_point_set_processing_precondition(0 < angle_max && angle_max <= M_PI/2.);
    }

    template <class Edge>
    void operator()(Edge& edge, const MST_graph& mst_graph)
    {
        typedef typename boost::property_traits<NormalPMap>::value_type Vector;
        typedef typename MST_graph::vertex_descriptor vertex_descriptor;

        // Gets source normal
        vertex_descriptor source_vertex = boost::source(edge, mst_graph);
        const Vector source_normal = mst_graph.m_normal_pmap[mst_graph[source_vertex].input_point];
        const bool source_normal_is_oriented = mst_graph[source_vertex].is_oriented;

        // Gets target normal
        vertex_descriptor target_vertex = boost::target(edge, mst_graph);
        Vector& target_normal = mst_graph.m_normal_pmap[mst_graph[target_vertex].input_point];
        bool& target_normal_is_oriented = ((MST_graph&)mst_graph)[target_vertex].is_oriented;

        if ( ! target_normal_is_oriented )
        {
          //             ->                        ->
          // Orients target_normal parallel to source_normal
          double normals_dot = source_normal * target_normal;
          if (normals_dot < 0)
            target_normal = -target_normal;

          // Is orientation robust?
          target_normal_is_oriented
            = source_normal_is_oriented &&
              (std::abs(normals_dot) >= std::cos(m_angle_max)); // oriented iff angle <= m_angle_max
        }
    }

// Data
// Implementation note: boost::breadth_first_search() makes copies of this object => data must be constant or shared.
private:
    const double m_angle_max; ///< max angle to propagate the normal orientation (radians).
};

/// Orients the normal of the point with maximum Z towards +Z axis.
///
/// @commentheading Template Parameters:
/// @param ForwardIterator iterator over input points.
/// @param PointPMap is a model of boost::ReadablePropertyMap with a value_type = Point_3<Kernel>.
/// @param NormalPMap is a model of boost::ReadWritePropertyMap with a value_type = Vector_3<Kernel>.
/// @param Kernel Geometric traits class.
///
/// @return iterator over the top point.
template <typename ForwardIterator,
          typename PointPMap,
          typename NormalPMap,
          typename Kernel
>
ForwardIterator
mst_find_source(
    ForwardIterator first,   ///< iterator over the first input point.
    ForwardIterator beyond,  ///< past-the-end iterator over the input points.
    PointPMap point_pmap, ///< property map ForwardIterator -> Point_3
    NormalPMap normal_pmap, ///< property map ForwardIterator -> Vector_3
    const Kernel& kernel)    ///< geometric traits.
{
    CGAL_TRACE("  mst_find_source()\n");

    // Input points types
    typedef typename boost::property_traits<NormalPMap>::value_type Vector;

    // Precondition: at least one element in the container
    CGAL_point_set_processing_precondition(first != beyond);

    // Find top point
    ForwardIterator top_point = first;
    for (ForwardIterator v = ++first; v != beyond; v++)
    {
      double top_z = get(point_pmap,top_point).z(); // top_point's Z coordinate
      double z = get(point_pmap,v).z();
      if (top_z < z)
        top_point = v;
    }

    // Orients its normal towards +Z axis
    Vector& normal = get(normal_pmap,top_point);
    const Vector Z(0, 0, 1);
    if (Z * normal < 0) {
      CGAL_TRACE("  Flip top point normal\n");
      normal = -normal;
    }

    return top_point;
}

/// Iterates over input points and creates Riemannian Graph:
/// - vertices are numbered like the input points index.
/// - vertices contain the corresponding input point iterator.
/// - we add the edge (i, j) if either vertex i is in the k-neighborhood of vertex j,
///   or vertex j is in the k-neighborhood of vertex i.
///
/// @commentheading Preconditions:
/// - Normals must be unit vectors.
/// - k >= 2.
///
/// @commentheading Template Parameters:
/// @param ForwardIterator iterator over input points.
/// @param IndexPMap is a model of boost::ReadablePropertyMap with an integral value_type.
/// @param PointPMap is a model of boost::ReadablePropertyMap with a value_type = Point_3<Kernel>.
/// @param NormalPMap is a model of boost::ReadWritePropertyMap with a value_type = Vector_3<Kernel>.
/// @param Kernel Geometric traits class.
///
/// @return the Riemannian graph
template <typename ForwardIterator,
          typename PointPMap,
          typename NormalPMap,
          typename IndexPMap,
          typename Kernel
>
Riemannian_graph<ForwardIterator>
create_riemannian_graph(
    ForwardIterator first,  ///< iterator over the first input point.
    ForwardIterator beyond, ///< past-the-end iterator over the input points.
    PointPMap point_pmap, ///< property map ForwardIterator -> Point_3
    NormalPMap normal_pmap, ///< property map ForwardIterator -> Vector_3
    IndexPMap index_pmap, ///< property map ForwardIterator -> index
    unsigned int k, ///< number of neighbors
    const Kernel& kernel) ///< geometric traits.
{
    // Input points types
    typedef typename boost::property_traits<PointPMap>::value_type Point;
    typedef typename boost::property_traits<NormalPMap>::value_type Vector;

    // Types for K nearest neighbors search structure
    typedef Point_vertex_handle_3<ForwardIterator> Point_vertex_handle_3;
    typedef Search_traits_vertex_handle_3<ForwardIterator> Traits;
    typedef Euclidean_distance_vertex_handle_3<ForwardIterator> KDistance;
    typedef Orthogonal_k_neighbor_search<Traits,KDistance> Neighbor_search;
    typedef typename Neighbor_search::Tree Tree;
    typedef typename Neighbor_search::iterator Search_iterator;

    // Riemannian_graph types
    typedef CGALi::Riemannian_graph<ForwardIterator> Riemannian_graph;
    typedef typename boost::property_map<Riemannian_graph, boost::edge_weight_t>::type Riemannian_graph_weight_map;

    // Precondition: at least one element in the container.
    CGAL_point_set_processing_precondition(first != beyond);

    // Precondition: at least 2 nearest neighbors
    CGAL_point_set_processing_precondition(k >= 2);

    // Number of input points
    const int num_input_points = distance(first, beyond);

    long memory = CGAL::Memory_sizer().virtual_size(); CGAL_TRACE("  %ld Mb allocated\n", memory>>20);
    CGAL_TRACE("  Creates KD-tree\n");

    // Instanciate a KD-tree search.
    // Notes: We have to wrap each input point by a Point_vertex_handle_3.
    //        The KD-tree is allocated dynamically to recover RAM as soon as possible.
    std::vector<Point_vertex_handle_3> kd_tree_points; kd_tree_points.reserve(num_input_points);
    for (ForwardIterator it = first; it != beyond; it++)
    {
        Point point = get(point_pmap, it);
        Point_vertex_handle_3 point_wrapper(point.x(), point.y(), point.z(), it);
        kd_tree_points.push_back(point_wrapper);
    }
    std::auto_ptr<Tree> tree( new Tree(kd_tree_points.begin(), kd_tree_points.end()) );

    // Recover RAM
    kd_tree_points.clear();

    /*long*/ memory = CGAL::Memory_sizer().virtual_size(); CGAL_TRACE("  %ld Mb allocated\n", memory>>20);
    CGAL_TRACE("  Creates Riemannian Graph\n");

    // Iterates over input points and creates Riemannian Graph:
    // - vertices are numbered like the input points index.
    // - vertices contain the corresponding input point iterator.
    // - we add the edge (i, j) if either vertex i is in the k-neighborhood of vertex j,
    //   or vertex j is in the k-neighborhood of vertex i.
    Riemannian_graph riemannian_graph;
    //
    // add vertices
    for (ForwardIterator it = first; it != beyond; it++)
    {
        typename Riemannian_graph::vertex_descriptor v = add_vertex(riemannian_graph);
        CGAL_point_set_processing_assertion(v == get(index_pmap,it));
        riemannian_graph[v].input_point = it;
    }
    //
    // add edges
    Riemannian_graph_weight_map riemannian_graph_weight_map = get(boost::edge_weight, riemannian_graph);
    for (ForwardIterator it = first; it != beyond; it++)
    {
        unsigned int it_index = get(index_pmap,it);
        Vector it_normal_vector = get(normal_pmap,it);

        // Gather set of (k+1) neighboring points.
        // Perform k+1 queries (as in point set, the query point is
        // output first). Search may be aborted if k is greater
        // than number of input points.
        Point point = get(point_pmap, it);
        Point_vertex_handle_3 point_wrapper(point.x(), point.y(), point.z(), it);
        Neighbor_search search(*tree, point_wrapper, k+1);
        Search_iterator search_iterator = search.begin();
        for(unsigned int i=0;i<(k+1);i++)
        {
            if(search_iterator == search.end())
                break; // premature ending

            ForwardIterator neighbor = search_iterator->first;
            unsigned int neighbor_index = get(index_pmap,neighbor);
            if (neighbor_index > it_index) // undirected graph
            {
                // Add edge
                typename boost::graph_traits<Riemannian_graph>::edge_descriptor e;
                bool inserted;
                boost::tie(e, inserted) = boost::add_edge(boost::vertex(it_index, riemannian_graph),
                                                          boost::vertex(neighbor_index, riemannian_graph),
                                                          riemannian_graph);
                CGAL_point_set_processing_assertion(inserted);

                //                               ->        ->
                // Computes edge weight = 1 - | normal1 * normal2 |
                // where normal1 and normal2 are the normal at the edge extremities.
                Vector neighbor_normal_vector = get(normal_pmap,neighbor);
                double weight = 1.0 - std::abs(it_normal_vector * neighbor_normal_vector);
                if (weight < 0)
                    weight = 0; // safety check
                riemannian_graph_weight_map[e] = (float)weight;
            }

            search_iterator++;
        }
    }

    return riemannian_graph;
}

/// Computes Minimum Spanning Tree and store it in a Boost graph:
/// - vertices are numbered like the input points index.
/// - vertices contain the corresponding input point iterator.
/// - we add the edge (predecessor[i], i) for each element of the MST.
///
/// @commentheading Preconditions:
/// - Normals must be unit vectors.
///
/// @commentheading Template Parameters:
/// @param ForwardIterator iterator over input points.
/// @param IndexPMap is a model of boost::ReadablePropertyMap with an integral value_type.
/// @param PointPMap is a model of boost::ReadablePropertyMap with a value_type = Point_3<Kernel>.
/// @param NormalPMap is a model of boost::ReadWritePropertyMap with a value_type = Vector_3<Kernel>.
/// @param Kernel Geometric traits class.
///
/// @return the MST graph.
template <typename ForwardIterator,
          typename PointPMap,
          typename NormalPMap,
          typename IndexPMap,
          typename Kernel
>
MST_graph<ForwardIterator, NormalPMap, Kernel>
create_mst_graph(
    ForwardIterator first,  ///< iterator over the first input point.
    ForwardIterator beyond, ///< past-the-end iterator over the input points.
    PointPMap point_pmap, ///< property map ForwardIterator -> Point_3
    NormalPMap normal_pmap, ///< property map ForwardIterator -> Vector_3
    IndexPMap index_pmap, ///< property map ForwardIterator -> index
    unsigned int k, ///< number of neighbors
    const Kernel& kernel, ///< geometric traits.
    const Riemannian_graph<ForwardIterator>& riemannian_graph, ///< graph connecting each vertex to its knn
    ForwardIterator source_point) ///< source point (with an oriented normal)
{
    // Bring private stuff to scope
    using namespace CGALi;

    // Input points types
    typedef typename boost::property_traits<PointPMap>::value_type Point;
    typedef typename boost::property_traits<NormalPMap>::value_type Vector;

    // Riemannian_graph types
    typedef CGALi::Riemannian_graph<ForwardIterator> Riemannian_graph;
    typedef typename boost::property_map<Riemannian_graph, boost::edge_weight_t>::const_type Riemannian_graph_weight_map;

    // MST_graph types
    typedef CGALi::MST_graph<ForwardIterator, NormalPMap, Kernel> MST_graph;

    // Precondition: at least one element in the container.
    CGAL_point_set_processing_precondition(first != beyond);

    // Number of input points
    const int num_input_points = boost::num_vertices(riemannian_graph);

    long memory = CGAL::Memory_sizer().virtual_size(); CGAL_TRACE("  %ld Mb allocated\n", memory>>20);
    CGAL_TRACE("  Calls boost::prim_minimum_spanning_tree()\n");

    // Computes Minimum Spanning Tree.
    unsigned int source_point_index = get(index_pmap, source_point);
    Riemannian_graph_weight_map riemannian_graph_weight_map = get(boost::edge_weight, riemannian_graph);
    typedef std::vector<typename Riemannian_graph::vertex_descriptor> PredecessorMap;
    PredecessorMap predecessor(num_input_points);
    boost::prim_minimum_spanning_tree(riemannian_graph, &predecessor[0],
                                      weight_map( riemannian_graph_weight_map )
                                     .root_vertex( boost::vertex(source_point_index, riemannian_graph) ));

    /*long*/ memory = CGAL::Memory_sizer().virtual_size(); CGAL_TRACE("  %ld Mb allocated\n", memory>>20);
    CGAL_TRACE("  Creates MST Graph\n");

    // Converts predecessor map to a MST graph:
    // - vertices are numbered like the input points index.
    // - vertices contain the corresponding input point iterator.
    // - we add the edge (predecessor[i], i) for each element of the predecessor map.
    MST_graph mst_graph(normal_pmap);
    //
    // Add vertices. source_point is the unique point marked "oriented".
    for (ForwardIterator it = first; it != beyond; it++)
    {
        typename MST_graph::vertex_descriptor v = add_vertex(mst_graph);
        CGAL_point_set_processing_assertion(v == get(index_pmap,it));
        mst_graph[v].input_point = it;
        mst_graph[v].is_oriented = (it == source_point);
    }
    // add edges
    for (unsigned int i=0; i < predecessor.size(); i++) // add edges
    {
        if (i != predecessor[i])
        {
            // check that bi-directed graph is useless
            CGAL_point_set_processing_assertion(predecessor[predecessor[i]] != i);

            boost::add_edge(boost::vertex(predecessor[i], mst_graph),
                            boost::vertex(i,     mst_graph),
                            mst_graph);
        }
    }

    return mst_graph;
}


} /* namespace CGALi */


// ----------------------------------------------------------------------------
// Public section
// ----------------------------------------------------------------------------


/// Orients the normals of the [first, beyond) range of points using the propagation
/// of a seed orientation through a minimum spanning tree of the Riemannian graph [Hoppe92].
/// This method modifies the order of input points so as to pack all sucessfully oriented points first,
/// and returns an iterator over the first point with an unoriented normal (see erase-remove idiom).
/// For this reason it should not be called on sorted containers.
///
/// @commentheading Preconditions:
/// - Normals must be unit vectors.
/// - k >= 2.
///
/// @commentheading Template Parameters:
/// @param ForwardIterator iterator over input points.
/// @param PointPMap is a model of boost::ReadablePropertyMap with a value_type = Point_3<Kernel>.
///        It can be omitted if ForwardIterator value_type is convertible to Point_3<Kernel>.
/// @param NormalPMap is a model of boost::ReadWritePropertyMap with a value_type = Vector_3<Kernel>.
/// @param IndexPMap must be a model of boost::ReadablePropertyMap with an integral value_type.
///        It can be omitted and will default to a std::map<ForwardIterator,int>.
/// @param Kernel Geometric traits class.
///        It can be omitted and deduced automatically from PointPMap value_type.
///
/// @return iterator over the first point with an unoriented normal.

// This variant requires all parameters.
template <typename ForwardIterator,
          typename PointPMap,
          typename NormalPMap,
          typename IndexPMap,
          typename Kernel
>
ForwardIterator
mst_orient_normals(
    ForwardIterator first,  ///< iterator over the first input point.
    ForwardIterator beyond, ///< past-the-end iterator over the input points.
    PointPMap point_pmap, ///< property map ForwardIterator -> Point_3.
    NormalPMap normal_pmap, ///< property map ForwardIterator -> Vector_3.
    IndexPMap index_pmap, ///< property map ForwardIterator -> index
    unsigned int k, ///< number of neighbors
    const Kernel& kernel) ///< geometric traits.
{
    CGAL_TRACE("Calls mst_orient_normals()\n");

    // Bring private stuff to scope
    using namespace CGALi;

    // Input points types
    typedef typename boost::property_traits<NormalPMap>::value_type Vector;

    // Riemannian_graph types
    typedef Riemannian_graph<ForwardIterator> Riemannian_graph;

    // MST_graph types
    typedef MST_graph<ForwardIterator, NormalPMap, Kernel> MST_graph;

    // Precondition: at least one element in the container.
    CGAL_point_set_processing_precondition(first != beyond);

    // Precondition: at least 2 nearest neighbors
    CGAL_point_set_processing_precondition(k >= 2);

    // Orients the normal of the point with maximum Z towards +Z axis.
    ForwardIterator source_point
      = mst_find_source(first, beyond,
                        point_pmap, normal_pmap,
                        kernel);

    // Iterates over input points and creates Riemannian Graph:
    // - vertices are numbered like the input points index.
    // - vertices are empty.
    // - we add the edge (i, j) if either vertex i is in the k-neighborhood of vertex j,
    //   or vertex j is in the k-neighborhood of vertex i.
    Riemannian_graph riemannian_graph
      = create_riemannian_graph(first, beyond,
                                point_pmap, normal_pmap, index_pmap,
                                k,
                                kernel);

    // Creates a Minimum Spanning Tree starting at source_point
    MST_graph mst_graph = create_mst_graph(first, beyond,
                                           point_pmap, normal_pmap, index_pmap,
                                           k,
                                           kernel,
                                           riemannian_graph,
                                           source_point);

    long memory = CGAL::Memory_sizer().virtual_size(); CGAL_TRACE("  %ld Mb allocated\n", memory>>20);
    CGAL_TRACE("  Calls boost::breadth_first_search()\n");

    // Traverse the point set along the MST to propagate source_point's orientation
    Propagate_normal_orientation<ForwardIterator, NormalPMap, Kernel> orienter;
    unsigned int source_point_index = get(index_pmap, source_point);
    boost::breadth_first_search(mst_graph,
                                boost::vertex(source_point_index, mst_graph), // source
                                visitor(boost::make_bfs_visitor(orienter)));

    // Copy points with robust normal orientation to oriented_points[], the others to unoriented_points[].
    std::deque<Enriched_point> oriented_points, unoriented_points;
    for (ForwardIterator it = first; it != beyond; it++)
    {
        unsigned int it_index = get(index_pmap,it);
        typename MST_graph::vertex_descriptor v = boost::vertex(it_index, mst_graph);
        if (mst_graph[v].is_oriented)
          oriented_points.push_back(*it);
        else
          unoriented_points.push_back(*it);
    }

    // Replaces [first, beyond) range by the content of oriented_points[], then unoriented_points[].
    ForwardIterator first_unoriented_point =
      std::copy(oriented_points.begin(), oriented_points.end(), first);
    std::copy(unoriented_points.begin(), unoriented_points.end(), first_unoriented_point);

    // At this stage, we have typically 0 unoriented normals if k is large enough
    CGAL_TRACE("  => %u normals are unoriented\n", unoriented_points.size());

    /*long*/ memory = CGAL::Memory_sizer().virtual_size(); CGAL_TRACE("  %ld Mb allocated\n", memory>>20);
    CGAL_TRACE("End of mst_orient_normals()\n");

    return first_unoriented_point;
}

/// @cond SKIP_IN_MANUAL
// This variant deduces the kernel from the point property map.
template <typename ForwardIterator,
          typename PointPMap,
          typename NormalPMap,
          typename IndexPMap
>
ForwardIterator
mst_orient_normals(
    ForwardIterator first,  ///< iterator over the first input point.
    ForwardIterator beyond, ///< past-the-end iterator over the input points.
    PointPMap point_pmap, ///< property map ForwardIterator -> Point_3.
    NormalPMap normal_pmap, ///< property map ForwardIterator -> Vector_3.
    IndexPMap index_pmap, ///< property map ForwardIterator -> index
    unsigned int k) ///< number of neighbors
{
    typedef typename boost::property_traits<PointPMap>::value_type Point;
    typedef typename Kernel_traits<Point>::Kernel Kernel;
    return mst_orient_normals(
      first,beyond,
      point_pmap,
      normal_pmap,
      index_pmap,
      k,
      Kernel());
}
/// @endcond

/// @cond SKIP_IN_MANUAL
// This variant creates a default index property map = std::map<ForwardIterator,int>.
template <typename ForwardIterator,
          typename PointPMap,
          typename NormalPMap
>
ForwardIterator
mst_orient_normals(
    ForwardIterator first,  ///< iterator over the first input point.
    ForwardIterator beyond, ///< past-the-end iterator over the input points.
    PointPMap point_pmap, ///< property map ForwardIterator -> Point_3
    NormalPMap normal_pmap, ///< property map ForwardIterator -> Vector_3
    unsigned int k) ///< number of neighbors
{
    CGAL_TRACE("Index input points in temporary std::map\n");

    // Index input points in temporary std::map
    typedef CGALi::Compare_iterator_address<ForwardIterator> Less;
    std::map<ForwardIterator,int,Less> index_map;
    ForwardIterator it;
    int index;
    for (it = first, index = 0; it != beyond; it++, index++)
      index_map[it] = index;

    // Wrap std::map in property map
    boost::associative_property_map< std::map<ForwardIterator,int,Less> >
      index_pmap(index_map);

    return mst_orient_normals(
      first,beyond,
      point_pmap,
      normal_pmap,
      index_pmap,
      k);
}
/// @endcond

/// @cond SKIP_IN_MANUAL
// This variant creates a default point property map = Dereference_property_map.
template <typename ForwardIterator,
          typename NormalPMap
>
ForwardIterator
mst_orient_normals(
    ForwardIterator first,  ///< iterator over the first input point.
    ForwardIterator beyond, ///< past-the-end iterator over the input points.
    NormalPMap normal_pmap, ///< property map ForwardIterator -> Vector_3.
    unsigned int k) ///< number of neighbors
{
    return mst_orient_normals(
      first,beyond,
      make_dereference_property_map(first),
      normal_pmap,
      k);
}
/// @endcond


CGAL_END_NAMESPACE

#endif // CGAL_MST_ORIENT_NORMALS_H

