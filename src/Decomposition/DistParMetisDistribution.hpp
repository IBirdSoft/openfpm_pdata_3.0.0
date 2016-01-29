/*
 * DistParMetisDistribution.hpp
 *
 *  Created on: Nov 19, 2015
 *      Author: Antonio Leo
 */

#include "SubdomainGraphNodes.hpp"
#include "parmetis_dist_util.hpp"
#include "Graph/dist_map_graph.hpp"
#include "Graph/DistGraphFactory.hpp"
#ifndef SRC_DECOMPOSITION_DISTPARMETISDISTRIBUTION_HPP_
#define SRC_DECOMPOSITION_DISTPARMETISDISTRIBUTION_HPP_

template<unsigned int dim, typename T, template<unsigned int, typename > class Domain = Box>
class DistParMetisDistribution
{
	//! Vcluster
	Vcluster & v_cl;

	//! Structure that store the cartesian grid information
	grid_sm<dim, void> gr;

	//! rectangular domain to decompose
	Domain<dim, T> domain;

	//! Processor sub-sub-domain graph
	DistGraph_CSR<nm_v, nm_e> sub_g;

	//! Convert the graph to parmetis format
	DistParmetis<DistGraph_CSR<nm_v, nm_e>> parmetis_graph;

	//! Init vtxdist needed for Parmetis
	openfpm::vector<idx_t> vtxdist;

	//! partitions
	openfpm::vector<openfpm::vector<idx_t>> partitions;

	//! Init data structure to keep trace of new vertices distribution in processors (needed to update main graph)
	openfpm::vector<openfpm::vector<size_t>> v_per_proc;

	//! Number of moved vertices in all iterations
	size_t g_moved = 0;

	//! Max number of moved vertices in all iterations
	size_t m_moved = 0;

	//! Flag to check if weights are used on vertices
	bool verticesGotWeights = false;

	/*! \brief Callback of the sendrecv to set the size of the array received
	 *
	 * \param msg_i Index of the message
	 * \param total_msg Total numeber of messages
	 * \param total_p Total number of processors to comunicate with
	 * \param i Processor id
	 * \param ri Request id
	 * \param ptr Void pointer parameter for additional data to pass to the call-back
	 */
	static void * message_receive(size_t msg_i, size_t total_msg, size_t total_p, size_t i, size_t ri, void * ptr)
	{
		openfpm::vector < openfpm::vector < idx_t >> *v = static_cast<openfpm::vector<openfpm::vector<idx_t>> *>(ptr);

		v->get(i).resize(msg_i / sizeof(idx_t));

		return &(v->get(i).get(0));
	}

public:

	/*! Constructor for the ParMetis class
	 *
	 * @param v_cl Vcluster to use as communication object in this class
	 */
	DistParMetisDistribution(Vcluster & v_cl) :
			v_cl(v_cl), parmetis_graph(v_cl, v_cl.getProcessingUnits()), vtxdist(v_cl.getProcessingUnits() + 1), partitions(v_cl.getProcessingUnits()), v_per_proc(v_cl.getProcessingUnits())

	{
	}

	/*! \brief Initialize the distribution graph
	 *
	 * /param grid Grid
	 * /param dom Domain
	 */
	void init(grid_sm<dim, void> & grid, Domain<dim, T> dom)
	{
		//! Set grid and domain
		gr = grid;
		domain = dom;

		//! Create sub graph
		DistGraphFactory<dim, DistGraph_CSR<nm_v, nm_e>> dist_g_factory;
		sub_g = dist_g_factory.template construct<NO_EDGE, nm_v::id, nm_v::global_id, nm_e::srcgid, nm_e::dstgid, T, dim - 1, 0, 1, 2>(gr.getSize(), domain);
		sub_g.getDecompositionVector(vtxdist);
		for(size_t i=0; i< sub_g.getNVertex(); i++)
			sub_g.vertex(i).template get<nm_v::x>()[2] = 0;

	}

	/*! \brief Get the current graph (main)
	 *
	 */
	DistGraph_CSR<nm_v, nm_e> & getGraph()
	{
		return sub_g;
	}

	/*! \brief Create first decomposition, it divides the graph in slices and give each slice to a processor
	 *
	 */
	void decompose()
	{
		//! Init sub graph in parmetis format
		parmetis_graph.initSubGraph(sub_g);

		//! Decompose
		parmetis_graph.decompose<nm_v::proc_id>(sub_g);

		//! Get result partition for this processors
		idx_t *partition = parmetis_graph.getPartition();

		for (size_t i = 0; i < sub_g.getNVertex(); ++i)
		{
			if (partition[i] != v_cl.getProcessUnitID())
				sub_g.q_move(i, partition[i]);
		}
		sub_g.redistribute();

	}

	/*! \brief Refine current decomposition
	 *
	 * It makes a refinement of the current decomposition using Parmetis function RefineKWay
	 * After that it also does the remapping of the graph
	 *
	 */
	void refine()
	{
		//! Reset parmetis graph and reconstruct it
		parmetis_graph.reset(sub_g);

		//! Refine
		parmetis_graph.refine<nm_v::proc_id>(sub_g);

		//! Get result partition for this processors
		idx_t *partition = parmetis_graph.getPartition();

		for (size_t i = 0; i < sub_g.getNVertex(); ++i)
		{
			if (partition[i] != v_cl.getProcessUnitID())
				sub_g.q_move(i, partition[i]);
		}

		sub_g.redistribute();

		/*
		 for (size_t i = 0; i < sub_g.getNVertex(); i++)
		 {
		 // Add weight to vertex and migration cost
		 std::cout << "g " << sub_g.vertex(i).template get<nm_v::global_id>() << "-" << sub_g.vertex(i).template get<nm_v::id>() << "  -> ";

		 // Create the adjacency list and the weights for edges
		 for (size_t s = 0; s < sub_g.getNChilds(i); s++)
		 {
		 std::cout << sub_g.getChild(i, s) << " ";
		 }
		 std::cout << "\n";
		 }*/
	}

	/*! \brief Compute the unbalance value
	 *
	 * \return the unbalance value
	 */
	float getUnbalance()
	{
		long t_cost = 0;

		long min, max, sum;
		float unbalance;

		t_cost = getProcessorLoad();

		min = t_cost;
		max = t_cost;
		sum = t_cost;

		v_cl.min(min);
		v_cl.max(max);
		v_cl.sum(sum);
		v_cl.execute();

		unbalance = ((float) (max - min)) / (float) sum;

		return unbalance * 100;
	}

	/*! \brief function that return the position of the vertex in the space
	 *
	 * \param id vertex id
	 * \param pos vector that will contain x, y, z
	 *
	 */
	void getVertexPosition(size_t id, T (&pos)[dim])
	{
		if (id >= sub_g.getNVertex())
			std::cerr << "Position - Such vertex doesn't exist (id = " << id << ", " << "total size = " << sub_g.getNVertex() << ")\n";

		pos[0] = sub_g.vertex(id).template get<nm_v::x>()[0];
		pos[1] = sub_g.vertex(id).template get<nm_v::x>()[1];
		if (dim == 3)
			pos[2] = sub_g.vertex(id).template get<nm_v::x>()[2];
	}

	/*! \brief Function that set the weight of the vertex
	 *
	 * \param id vertex id
	 * \param weight to give to the vertex
	 *
	 */
	inline void setVertexWeight(size_t id, size_t weight)
	{
		if (!verticesGotWeights)
			verticesGotWeights = true;

		if (id >= sub_g.getNVertex())
			std::cerr << "Weight - Such vertex doesn't exist (id = " << id << ", " << "total size = " << sub_g.getNVertex() << ")\n";

		// If the vertex is inside this processor update the value
		sub_g.vertex(id).template get<nm_v::computation>() = weight;

	}

	/*! \brief Checks if weights are used on the vertices
	 *
	 * \return true if weights are used in the decomposition
	 */
	bool weightsAreUsed()
	{
		return verticesGotWeights;
	}

	/*! \brief function that get the weight of the vertex
	 *
	 * \param id vertex id
	 *
	 */
	size_t getVertexWeight(size_t id)
	{
		if (id >= sub_g.getNVertex())
			std::cerr << "Such vertex doesn't exist (id = " << id << ", " << "total size = " << sub_g.getTotNVertex() << ")\n";

		return sub_g.vertex(id).template get<nm_v::computation>();
	}

	/*! \brief Compute the processor load counting the total weights of its vertices
	 *
	 * \return the computational load of the processor graph
	 */
	size_t getProcessorLoad()
	{
		size_t load = 0;

		for (size_t i = 0; i < sub_g.getNVertex(); i++)
		{
			load += sub_g.vertex(i).template get<nm_v::computation>();
		}
		return load;
	}

	/*! \brief return number of moved vertices in all iterations so far
	 *
	 * \param id vertex id
	 *
	 * \return vector with x, y, z
	 *
	 */
	size_t getTotalMovedV()
	{
		return g_moved;
	}

	/*! \brief return number of moved vertices in all iterations so far
	 *
	 * \param id vertex id
	 *
	 * \return vector with x, y, z
	 *
	 */
	size_t getMaxMovedV()
	{
		return m_moved;
	}

	/*! \brief Set migration cost of the vertex id
	 *
	 * \param id of the vertex to update
	 * \param migration cost of the migration
	 */
	void setMigrationCost(size_t id, size_t migration)
	{
		if (id >= sub_g.getNVertex())
			std::cerr << "Migration - Such vertex doesn't exist (id = " << id << ", " << "total size = " << sub_g.getNVertex() << ")\n";

		sub_g.vertex(id).template get<nm_v::migration>() = migration;
	}

	/*! \brief Set communication cost of the edge id
	 *
	 * \param v_id Id of the source vertex of the edge
	 * \param e i child of the vertex
	 * \param communication Communication value
	 */
	void setCommunicationCost(size_t v_id, size_t e, size_t communication)
	{
		sub_g.getChildEdge(v_id, e).template get<nm_e::communication>() = communication;
	}

	/*! \brief Returns total number of sub-sub-domains in the distribution graph
	 *
	 */
	size_t getNSubSubDomains()
	{
		return sub_g.getNVertex();
	}

	/*! \brief Returns total number of neighbors of the sub-sub-domain id
	 *
	 * \param i id of the sub-sub-domain
	 */
	size_t getNSubSubDomainNeighbors(size_t id)
	{
		if (id >= sub_g.getNVertex())
			std::cerr << "Neighbors - Such vertex doesn't exist (id = " << id << ", " << "total size = " << sub_g.getNVertex() << ")\n";

		return sub_g.getNChilds(id);
	}

	/*! \brief Print current graph and save it to file with name test_graph_[id]
	 *
	 * \param id to attach to the filename
	 *
	 */
	void printCurrentDecomposition(int id)
	{
		if (v_cl.getProcessUnitID() == 0)
		{
			sub_g.deleteGhosts();

			for (size_t i = 0; i < sub_g.getTotNVertex(); ++i)
			{
				sub_g.reqVertex(i);
			}
		}

		sub_g.sync();

		if (v_cl.getProcessUnitID() == 0)
		{
			VTKWriter<DistGraph_CSR<nm_v, nm_e>, DIST_GRAPH> gv2(sub_g);
			gv2.write("test_dist_graph_" + std::to_string(id) + ".vtk");

			sub_g.deleteGhosts();
		}
	}
};

#endif /* SRC_DECOMPOSITION_PARMETISDISTRIBUTION_HPP_ */