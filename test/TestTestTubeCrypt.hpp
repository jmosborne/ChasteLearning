
/* CREATED BY: Phillip Brown
 * A Chaste test that approximates a crypt with stem cells and transit amplifying cells
 * A membrane force similar to that used by SJD is attempted
 */

#include <cxxtest/TestSuite.h> //Needed for all test files
#include "CellBasedSimulationArchiver.hpp" //Needed if we would like to save/load simulations
#include "AbstractCellBasedTestSuite.hpp" //Needed for cell-based tests: times simulations, generates random numbers and has cell properties
#include "CheckpointArchiveTypes.hpp" //Needed if we use GetIdentifier() method (which we do)
#include "SmartPointers.hpp" //Enables macros to save typing

/* The next set of classes are needed specifically for the simulation, which can be found in the core code. */

#include "HoneycombMeshGenerator.hpp" //Generates mesh
#include "CylindricalHoneycombMeshGenerator.hpp"
#include "OffLatticeSimulation.hpp" //Simulates the evolution of the population
#include "MeshBasedCellPopulationWithGhostNodes.hpp"
#include "VoronoiDataWriter.hpp" //Allows us to visualise output in Paraview
#include "DifferentiatedCellProliferativeType.hpp" //Stops cells from proliferating
#include "TransitCellProliferativeType.hpp"
#include "FakePetscSetup.hpp"
#include "GeneralisedLinearSpringForce.hpp" //give a force to use between cells

#include "UniformCellCycleModel.hpp"
#include "WildTypeCellMutationState.hpp"
#include "TransitCellProliferativeType.hpp"
#include "StemCellProliferativeType.hpp"
#include "EpithelialLayerBasementMembraneForce.hpp"
#include "EpithelialLayerBasementMembraneForceModified.hpp"
#include "EpithelialLayerLinearSpringForce.hpp"
#include "EpithelialLayerAnoikisCellKiller.hpp"

#include "AnoikisCellKillerMembraneCell.hpp"
#include "LinearSpringForceMembraneCell.hpp"
#include "MembraneCellForce.hpp" // A force to restore the membrane to it's preferred shape
#include "NoCellCycleModel.hpp"

#include "TransitCellAnoikisResistantMutationState.hpp"
#include "CryptBoundaryCondition.hpp"
#include "BoundaryCellProperty.hpp"

#include "LinearSpringSmallMembraneCell.hpp" // Just to make sure this force works in a different simulation

class TestBasicTestTubeCrypt : public AbstractCellBasedTestSuite
{
	public:
	void TestTubeCryptCell() throw(Exception)
	{
		unsigned cells_up = 30;
		unsigned cells_across = 30;
		unsigned ghosts = 4;

		double dt = 0.005;
		double end_time = 100;
		double sampling_multiple = 100;

		//Set all the spring stiffness variables
		double epithelialStiffness = 15.0; //Epithelial-epithelial spring connections
		double membraneStiffness = 20.0; //Stiffness of membrane to membrane spring connections
		double stromalStiffness = 15.0;

		double epithelialMembraneStiffness = 15.0; //Epithelial-non-epithelial spring connections
		double membraneStromalStiffness = 5.0; //Non-epithelial-non-epithelial spring connections
		double stromalEpithelialStiffness = 10.0;

		double torsional_stiffness = 25.0;
		double stiffness_ratio = 4.5; // For paneth cells
		
		double targetCurvatureStemStem = 1/5;
		double targetCurvatureStemTrans = 0; // Not implemented properly, so keep it the same as TransTrans for now
		double targetCurvatureTransTrans = 0;

		double lumen_left_edge = 6.5;
		double lumen_right_edge = 13.5;
		double lumen_bottom = 4.5;


		//Copied wholesale from Axel
		//used to make the base of the test tube shaped crypt
		c_vector<double,2> circle_centre;
		circle_centre(0) = cells_across/2;
		circle_centre(1) = 10;

		double circle_radius = 5; //Size of hole

		double ring_width = 0.9;

		//Start off with a mesh
		CylindricalHoneycombMeshGenerator generator(cells_across, cells_up, ghosts);
		Cylindrical2dMesh* p_mesh = generator.GetCylindricalMesh();
		// HoneycombMeshGenerator generator(cells_up, cells_across, ghosts);
		// MutableMesh<2,2>* p_mesh = generator.GetMesh();

		//Sort through the indices and decide which ones are ghost nodes
		std::vector<unsigned> initial_real_indices = generator.GetCellLocationIndices();
		std::vector<unsigned> real_indices;
		std::set<unsigned> real_indices_set;

		//for each index
			//check if index falls in the crypt lumen
				//if it doesn't, add to list of non-ghost indices

		for (unsigned i = 0; i < initial_real_indices.size(); i++)
		{
			unsigned cell_index = initial_real_indices[i];
			double x = p_mesh->GetNode(cell_index)->rGetLocation()[0];
			double y = p_mesh->GetNode(cell_index)->rGetLocation()[1];

			//Make the curved crypt base
			if ( (pow(x-circle_centre[0],2) + pow(y-circle_centre[1],2) > pow(circle_radius,2)) && y<= circle_centre[1])
			{
				real_indices.push_back(cell_index);
				real_indices_set.insert(cell_index);

			}

			if ( ((x <= circle_centre[0] - circle_radius -.5) || (x >= circle_centre[0] + circle_radius + .5)) && (y > circle_centre[1]))
			{
				real_indices.push_back(cell_index);
				real_indices_set.insert(cell_index);
			}
		}

		//When instantiating a cell, we need to provide a mutation state
		boost::shared_ptr<AbstractCellProperty> p_state = CellPropertyRegistry::Instance()->Get<WildTypeCellMutationState>();
		boost::shared_ptr<AbstractCellProperty> p_trans_type = CellPropertyRegistry::Instance()->Get<TransitCellProliferativeType>();
		boost::shared_ptr<AbstractCellProperty> p_diff_type = CellPropertyRegistry::Instance()->Get<DifferentiatedCellProliferativeType>();
		boost::shared_ptr<AbstractCellProperty> p_stem_type = CellPropertyRegistry::Instance()->Get<StemCellProliferativeType>();

		boost::shared_ptr<AbstractCellProperty> p_boundary = CellPropertyRegistry::Instance()->Get<BoundaryCellProperty>();

		//create a vector to store the cells; it is currently empty
		std::vector<CellPtr> cells;

		//go through the real indices and build some cells
		for (unsigned i = 0; i<real_indices.size(); i++)
		{	
			//Set cell cycle
			UniformCellCycleModel* p_cycle_model = new UniformCellCycleModel();
			//p_cycle_model->SetCellCycleDuration(); //randomly chooses a duration
			double birth_time = 12.0*RandomNumberGenerator::Instance()->ranf(); //Randomly set birth time to stop pulsing behaviour
			p_cycle_model->SetBirthTime(-birth_time);
			

			CellPtr p_cell(new Cell(p_state, p_cycle_model));

			unsigned cell_index = real_indices[i];
			double x = p_mesh->GetNode(cell_index)->rGetLocation()[0];
			double y = p_mesh->GetNode(cell_index)->rGetLocation()[1];

			p_cell->SetCellProliferativeType(p_diff_type); //set the type to differentiated if it's not a ghost node - types will be reset as follows

			//add stems cells to the base of the crypt
			if ((pow(x-circle_centre[0],2) + pow(y-circle_centre[1],2) > pow(circle_radius,2)) && (pow(x-circle_centre[0],2) + pow(y-circle_centre[1],2) < pow(circle_radius + ring_width,2)) && y<= circle_centre[1])
			{
				p_cell->SetCellProliferativeType(p_stem_type); //set the cell to stem if it's at the base of the lumen
			}

			//add transit cells to the left edge of lumen
			if ( ((x <= circle_centre[0] - circle_radius -.5) && (x >= circle_centre[0] - circle_radius -1)) && (y > circle_centre[1]))
			{
				p_cell->SetCellProliferativeType(p_trans_type);
			}
			//add transit cells to the right edge of lumen
			if ( ((x >= circle_centre[0] + circle_radius +.5) && (x <= circle_centre[0] + circle_radius +1)) && (y > circle_centre[1]))
			{
				p_cell->SetCellProliferativeType(p_trans_type); 
			}
			if (y==0){
				p_cell->AddCellProperty(p_boundary);
			}

			p_cell->InitialiseCellCycleModel();

			cells.push_back(p_cell);
		}

		//Pull it all together
		MeshBasedCellPopulationWithGhostNodes<2> cell_population(*p_mesh, cells, real_indices);

		cell_population.AddPopulationWriter<VoronoiDataWriter>();

		/* Define the simulation class. */
		OffLatticeSimulation<2> simulator(cell_population);

		//Set output directory
		simulator.SetOutputDirectory("TestCryptBMCells");
        simulator.SetEndTime(end_time);
        simulator.SetDt(dt);
        simulator.SetSamplingTimestepMultiple(sampling_multiple);

        /* Add an anoikis-based cell killer. */
		MAKE_PTR_ARGS(AnoikisCellKillerMembraneCell, p_anoikis_killer, (&cell_population));
		simulator.AddCellKiller(p_anoikis_killer);

        // MAKE_PTR(GeneralisedLinearSpringForce<2>, p_force);
        // simulator.AddForce(p_force);

        //Basement membrane force

		MAKE_PTR(LinearSpringForceMembraneCell<2>, p_spring_force);
		p_spring_force->SetCutOffLength(1.5);
		//Set the spring stiffnesses
		p_spring_force->SetEpithelialSpringStiffness(epithelialStiffness);
		p_spring_force->SetMembraneSpringStiffness(membraneStiffness);
		p_spring_force->SetStromalSpringStiffness(stromalStiffness);
		p_spring_force->SetEpithelialMembraneSpringStiffness(epithelialMembraneStiffness);
		p_spring_force->SetMembraneStromalSpringStiffness(membraneStromalStiffness);
		p_spring_force->SetStromalEpithelialSpringStiffness(stromalEpithelialStiffness);

		p_spring_force->SetPanethCellStiffnessRatio(stiffness_ratio);
		simulator.AddForce(p_spring_force);

		MAKE_PTR_ARGS(CryptBoundaryCondition, p_bc, (&cell_population));
		simulator.AddCellPopulationBoundaryCondition(p_bc);

		//mutate a cell so it does not die from anoikis
        boost::shared_ptr<AbstractCellProperty> p_state_mutated = CellPropertyRegistry::Instance()->Get<TransitCellAnoikisResistantMutationState>();
        //"mutate" a differentiated cell if it is under the monolayer
        boost::shared_ptr<AbstractCellProperty> p_membrane = CellPropertyRegistry::Instance()->Get<MembraneCellProliferativeType>();



        // Make sure we have a monolayer, and pick a cell to make mutant
        for (AbstractCellPopulation<2>::Iterator cell_iter = cell_population.Begin();
             cell_iter != cell_population.End();
             ++cell_iter)
        {
            unsigned node_index = cell_population.GetLocationIndexUsingCell(*cell_iter);

            if (node_index == 822) // Chosen from looking at the results from steady state
            {
                //cell_iter->SetMutationState(p_state_mutated);
            }
            //if cell type is epithelial or stem, then make sure it's a monolayer and make all the connected differentiated cells a mutated type (if they haven't already)
            if (cell_iter->GetCellProliferativeType()->IsType<TransitCellProliferativeType>() || cell_iter->GetCellProliferativeType()->IsType<StemCellProliferativeType>())
            {
	            std::set<unsigned> neighbouring_node_indices = cell_population.GetNeighbouringNodeIndices(node_index);
	            //loop to make sure we have a monolayer
	            unsigned real_neighbour_count=0;
	            for (std::set<unsigned>::iterator iter = neighbouring_node_indices.begin();
	         			iter != neighbouring_node_indices.end();
	         				++iter)
	    		{
	    			//count the number of non-ghost neighbours
	    			if (real_indices_set.find(*iter) != real_indices_set.end())
	    			{
	    				real_neighbour_count +=1;
	    			}
	    		}
	    		//if the cell doesn't have any ghost neighbours, then it's not a monolayer, so convert back to differntiated type
	    		if (real_neighbour_count == neighbouring_node_indices.size())
	    		{
	    			//std::cout <<"Changed "<< node_index << std::endl;
	    			cell_iter->SetCellProliferativeType(p_diff_type);
	    		} else {
	    		//loop to make the membrane cells
	    			//loop through neighbours
		    		for (std::set<unsigned>::iterator iter = neighbouring_node_indices.begin();
		         			iter != neighbouring_node_indices.end();
		         				++iter)
		    		{
		    			if (real_indices_set.find(*iter) != real_indices_set.end()) //make sure the node is not a ghost first
		    			{
		    				CellPtr neighbour = cell_population.GetCellUsingLocationIndex(*iter);
		    				//check if the cell type is differentiated, then if it is, add the "mutation"
			    			if (neighbour->GetCellProliferativeType()->IsType<DifferentiatedCellProliferativeType>())
			    			{
								NoCellCycleModel* p_no_cycle_model = new NoCellCycleModel();
								neighbour->SetCellProliferativeType(p_membrane);
								neighbour->SetCellCycleModel(p_no_cycle_model);
			    			}
		    			}
		    		}
		    	}
	    	}
        }

        

        //Make the force for the membrane
        MAKE_PTR(MembraneCellForce, p_membrane_force);
        p_membrane_force->SetBasementMembraneTorsionalStiffness(torsional_stiffness);
        p_membrane_force->SetTargetCurvatures(targetCurvatureStemStem, targetCurvatureStemTrans, targetCurvatureTransTrans);
        simulator.AddForce(p_membrane_force);

        simulator.Solve();

	};

	void xTestTubeCryptForce() throw(Exception)
	{
		unsigned cells_up = 30;
		unsigned cells_across = 30;
		unsigned ghosts = 4;

		double dt = 0.005;
		double end_time = 1;
		double sampling_multiple = 10;

		//Set all the spring stiffness variables
		double epithelialStiffness = 15.0; //Epithelial-epithelial spring connections
		double membraneStiffness = 20.0; //Stiffness of membrane to membrane spring connections
		double stromalStiffness = 15.0;

		double epithelialMembraneStiffness = 15.0; //Epithelial-non-epithelial spring connections
		double membraneStromalStiffness = 5.0; //Non-epithelial-non-epithelial spring connections
		double stromalEpithelialStiffness = 10.0;

		double torsional_stiffness = 25.0;
		double stiffness_ratio = 4.5; // For paneth cells
		
		double targetCurvatureStemStem = 0.2;
		double targetCurvatureStemTrans = 0; // Not implemented properly, so keep it the same as TransTrans for now
		double targetCurvatureTransTrans = 0;

		double lumen_left_edge = 6.5;
		double lumen_right_edge = 13.5;
		double lumen_bottom = 4.5;


		//double stiffness_ratio = 4.5; // For paneth cells

		double bm_force = 10.0; //Set the basement membrane stiffness
		double target_curvature = 0.2; //Set the target curvature, i.e. how circular the layer wants to be

		//Copied wholesale from Axel
		//used to make the base of the test tube shaped crypt
		c_vector<double,2> circle_centre;
		circle_centre(0) = cells_across/2;
		circle_centre(1) = 10;

		double circle_radius = 5; //Size of hole

		double ring_width = 0.9;

		//Start off with a mesh
		// HoneycombMeshGenerator generator(cells_up, cells_across, ghosts);
		// MutableMesh<2,2>* p_mesh = generator.GetMesh();
		CylindricalHoneycombMeshGenerator generator(cells_across, cells_up, ghosts);
		Cylindrical2dMesh* p_mesh = generator.GetCylindricalMesh();

		//Sort through the indices and decide which ones are ghost nodes
		std::vector<unsigned> initial_real_indices = generator.GetCellLocationIndices();
		std::vector<unsigned> real_indices;
		std::set<unsigned> real_indices_set;

		//for each index
			//check if index falls in the crypt lumen
				//if it doesn't, add to list of non-ghost indices

		for (unsigned i = 0; i < initial_real_indices.size(); i++)
		{
			unsigned cell_index = initial_real_indices[i];
			double x = p_mesh->GetNode(cell_index)->rGetLocation()[0];
			double y = p_mesh->GetNode(cell_index)->rGetLocation()[1];

			//Make the curved crypt base
			if ( (pow(x-circle_centre[0],2) + pow(y-circle_centre[1],2) > pow(circle_radius,2)) && y<= circle_centre[1])
			{
				real_indices.push_back(cell_index);
				real_indices_set.insert(cell_index);

			}

			if ( ((x <= circle_centre[0] - circle_radius -.5) || (x >= circle_centre[0] + circle_radius + .5)) && (y > circle_centre[1]))
			{
				real_indices.push_back(cell_index);
				real_indices_set.insert(cell_index);
			}
		}

		//When instantiating a cell, we need to provide a mutation state
		boost::shared_ptr<AbstractCellProperty> p_state = CellPropertyRegistry::Instance()->Get<WildTypeCellMutationState>();
		boost::shared_ptr<AbstractCellProperty> p_trans_type = CellPropertyRegistry::Instance()->Get<TransitCellProliferativeType>();
		boost::shared_ptr<AbstractCellProperty> p_diff_type = CellPropertyRegistry::Instance()->Get<DifferentiatedCellProliferativeType>();
		boost::shared_ptr<AbstractCellProperty> p_stem_type = CellPropertyRegistry::Instance()->Get<StemCellProliferativeType>();

		//create a vector to store the cells; it is currently empty
		std::vector<CellPtr> cells;

		//go through the real indices and build some cells
		for (unsigned i = 0; i<real_indices.size(); i++)
		{	
			//Set cell cycle
			UniformCellCycleModel* p_cycle_model = new UniformCellCycleModel();
			//p_cycle_model->SetCellCycleDuration(); //randomly chooses a duration
			double birth_time = 12.0*RandomNumberGenerator::Instance()->ranf(); //Randomly set birth time to stop pulsing behaviour
			p_cycle_model->SetBirthTime(-birth_time);
			

			CellPtr p_cell(new Cell(p_state, p_cycle_model));

			unsigned cell_index = real_indices[i];
			double x = p_mesh->GetNode(cell_index)->rGetLocation()[0];
			double y = p_mesh->GetNode(cell_index)->rGetLocation()[1];

			p_cell->SetCellProliferativeType(p_diff_type); //set the type to differentiated if it's not a ghost node - types will be reset as follows

			//add stems cells to the base of the crypt
			if ((pow(x-circle_centre[0],2) + pow(y-circle_centre[1],2) > pow(circle_radius,2)) && (pow(x-circle_centre[0],2) + pow(y-circle_centre[1],2) < pow(circle_radius + ring_width,2)) && y<= circle_centre[1])
			{
				p_cell->SetCellProliferativeType(p_stem_type); //set the cell to stem if it's at the base of the lumen
			}

			//add transit cells to the left edge of lumen
			if ( ((x <= circle_centre[0] - circle_radius -.5) && (x >= circle_centre[0] - circle_radius -1)) && (y > circle_centre[1]))
			{
				p_cell->SetCellProliferativeType(p_trans_type);
			}
			//add transit cells to the right edge of lumen
			if ( ((x >= circle_centre[0] + circle_radius +.5) && (x <= circle_centre[0] + circle_radius +1)) && (y > circle_centre[1]))
			{
				p_cell->SetCellProliferativeType(p_trans_type); 
			}
			

			p_cell->InitialiseCellCycleModel();

			cells.push_back(p_cell);
		}

		//Pull it all together
		MeshBasedCellPopulationWithGhostNodes<2> cell_population(*p_mesh, cells, real_indices);

		cell_population.AddPopulationWriter<VoronoiDataWriter>();

		/* Define the simulation class. */
		OffLatticeSimulation<2> simulator(cell_population);

		//Set output directory
		simulator.SetOutputDirectory("TestCryptBMForce");
        simulator.SetEndTime(end_time);
        simulator.SetDt(dt);
        simulator.SetSamplingTimestepMultiple(sampling_multiple);

        /* Add an anoikis-based cell killer. */
		MAKE_PTR_ARGS(EpithelialLayerAnoikisCellKiller, p_anoikis_killer, (&cell_population));
		simulator.AddCellKiller(p_anoikis_killer);

        MAKE_PTR(GeneralisedLinearSpringForce<2>, p_force);
        simulator.AddForce(p_force);


		MAKE_PTR(LinearSpringForceMembraneCell<2>, p_spring_force);
		p_spring_force->SetCutOffLength(1.5);
		//Set the spring stiffnesses
		p_spring_force->SetEpithelialSpringStiffness(epithelialStiffness);
		p_spring_force->SetMembraneSpringStiffness(membraneStiffness);
		p_spring_force->SetStromalSpringStiffness(stromalStiffness);
		p_spring_force->SetEpithelialMembraneSpringStiffness(epithelialMembraneStiffness);
		p_spring_force->SetMembraneStromalSpringStiffness(membraneStromalStiffness);
		p_spring_force->SetStromalEpithelialSpringStiffness(stromalEpithelialStiffness);

		p_spring_force->SetPanethCellStiffnessRatio(stiffness_ratio);
		simulator.AddForce(p_spring_force);

		//mutate a cell so it does not die from anoikis
        boost::shared_ptr<AbstractCellProperty> p_state_mutated = CellPropertyRegistry::Instance()->Get<TransitCellAnoikisResistantMutationState>();
        //"mutate" a differentiated cell if it is under the monolayer
        boost::shared_ptr<AbstractCellProperty> p_membrane = CellPropertyRegistry::Instance()->Get<MembraneCellProliferativeType>();



        // Make sure we have a monolayer, and pick a cell to make mutant
        for (AbstractCellPopulation<2>::Iterator cell_iter = cell_population.Begin();
             cell_iter != cell_population.End();
             ++cell_iter)
        {
            unsigned node_index = cell_population.GetLocationIndexUsingCell(*cell_iter);

            if (node_index == 822) // Chosen from looking at the results from steady state
            {
                //cell_iter->SetMutationState(p_state_mutated);
            }
            //if cell type is epithelial or stem, then make sure it's a monolayer and make all the connected differentiated cells a mutated type (if they haven't already)
            if (cell_iter->GetCellProliferativeType()->IsType<TransitCellProliferativeType>() || cell_iter->GetCellProliferativeType()->IsType<StemCellProliferativeType>())
            {
	            std::set<unsigned> neighbouring_node_indices = cell_population.GetNeighbouringNodeIndices(node_index);
	            //loop to make sure we have a monolayer
	            unsigned real_neighbour_count=0;
	            for (std::set<unsigned>::iterator iter = neighbouring_node_indices.begin();
	         			iter != neighbouring_node_indices.end();
	         				++iter)
	    		{
	    			//count the number of non-ghost neighbours
	    			if (real_indices_set.find(*iter) != real_indices_set.end())
	    			{
	    				real_neighbour_count +=1;
	    			}
	    		}
	    		//if the cell doesn't have any ghost neighbours, then it's not a monolayer, so convert back to differentiated type
	    		if (real_neighbour_count == neighbouring_node_indices.size())
	    		{
	    			//std::cout <<"Changed "<< node_index << std::endl;
	    			cell_iter->SetCellProliferativeType(p_diff_type);
	    		}
	    	}
        }

        // Now we have an ordered vector of membrane indices, starting at the left and going anticlockwise through the stem cell niche

        // Make the force for the membrane
        MAKE_PTR(EpithelialLayerBasementMembraneForce, p_bm_force);
		p_bm_force->SetBasementMembraneParameter(bm_force); //Equivalent to beta in SJD's papers
		p_bm_force->SetTargetCurvature(target_curvature); //This is equivalent to 1/R in SJD's papers
		simulator.AddForce(p_bm_force);

        // Stop it launching into the stratosphere
		MAKE_PTR_ARGS(CryptBoundaryCondition, p_bc, (&cell_population));
		simulator.AddCellPopulationBoundaryCondition(p_bc);

        simulator.Solve();

	};
};
// Code that might still be useful
// If I find a way to pass in the membrane cells, then it will save time finding them every step

// Make a list of membrane cells by index
      //   unsigned starting_membrane_index = 0;
      //   // Finding the first membrane cell
      //   for (AbstractCellPopulation<2>::Iterator cell_iter = cell_population.Begin();
      //        cell_iter != cell_population.End();
      //        ++cell_iter)
      //   {
      //   	unsigned node_index = cell_population.GetLocationIndexUsingCell(*cell_iter);
      //   	double x = p_mesh->GetNode(node_index)->rGetLocation()[0];
        	
      //   	// If we've got a membrane cell, check if it's at the end we want
      //   	if (cell_iter->GetMutationState()->IsType<DifferentiatedMembraneState>())
      //   	{
      //   		// Loop through neighbours and count number of membrane neighbours, if it's only one, then we have an end cell
      //   		std::set<unsigned> neighbouring_node_indices = cell_population.GetNeighbouringNodeIndices(node_index);
      //   		unsigned membrane_cell_neighbour_count=0;
	     //        for (std::set<unsigned>::iterator iter = neighbouring_node_indices.begin();
	     //     			iter != neighbouring_node_indices.end();
	     //     				++iter)
	    	// 	{
	    	// 		//count the number of membrane neighbours
	    	// 		if (!cell_population.IsGhostNode(*iter))
	    	// 		{
	    	// 			if (cell_population.GetCellUsingLocationIndex(*iter)->GetMutationState()->IsType<DifferentiatedMembraneState>())
		    // 			{
		    // 				membrane_cell_neighbour_count +=1;
		    // 			}
	    	// 		}
	    			
	    	// 	}
	     //    	if (x < cells_across/2 && membrane_cell_neighbour_count == 1) // We want it to be the left hand side free end
	     //    	{
	     //    		starting_membrane_index = node_index;
	     //    		break;
	     //    	}
      //   	} 
      //   }

      //   // With the starting membrane index, we can now build the membrane as a vector of indices
      //   std::set<unsigned> membrane_indices_set;
      //   std::vector<unsigned> membrane_indices;

      //   membrane_indices_set.insert(starting_membrane_index);
      //   membrane_indices.push_back(starting_membrane_index); // Duplication of effort because it's easier to find an entry in a set than a vector

      //   bool reached_final_membrane_cell = false;
      //   unsigned current_index = starting_membrane_index;

      //   while (!reached_final_membrane_cell)
      //   {
      //   	reached_final_membrane_cell = true; // Assume we're done until proven otherwise
      //   	// Loop through neighbours, find a membrane cell that isn't already accounted for
      //   	std::set<unsigned> neighbouring_node_indices = cell_population.GetNeighbouringNodeIndices(current_index);
	     //    for (std::set<unsigned>::iterator iter = neighbouring_node_indices.begin();
		    //      			iter != neighbouring_node_indices.end();
		    //      				++iter)
		    // {
		    // 	// If the neighbour is a membrane cell and not already in the list, add it and set it as the current cell
		    // 	if (!cell_population.IsGhostNode(*iter))
		    // 	{
		    // 		CellPtr neighbour_cell = cell_population.GetCellUsingLocationIndex(*iter);
		    // 		if (neighbour_cell->GetMutationState()->IsType<DifferentiatedMembraneState>() && membrane_indices_set.find(*iter) == membrane_indices_set.end())
			   //  	{
	     //    			membrane_indices_set.insert(*iter);
	     //    			membrane_indices.push_back(*iter);
	     //    			reached_final_membrane_cell = false;
	     //    			current_index = *iter;
	     //    			break;
			   //  	}
		    // 	}
		    	
		    // }
      //   }

        // Now we have an ordered vector of membrane indices, starting at the left and going anticlockwise through the stem cell niche