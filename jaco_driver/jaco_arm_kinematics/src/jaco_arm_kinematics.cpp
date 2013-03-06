/*
 * Copyright (c) 2012  DFKI GmbH, Bremen, Germany
 *
 *  This file is free software: you may copy, redistribute and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation, either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  This file is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *
 *  Author: Sankaranarayanan Natarajan / sankar.natarajan@dfki.de
 *
 *  FILE --- jaco_kinematics.cpp
 *
 *  PURPOSE ---  Source file for jaco arm kinematics. It uses a ik file generated by openrave ikfast.
 *               Based on pr2_arm_kinematics by Sachin Chitta and
 *	         Katana ros package by Henning Deeken
 */

#include <ros/ros.h>
#include <jaco_arm_kinematics/jaco_arm_kinematics.h>
#include <kdl_parser/kdl_parser.hpp>
#include <tf_conversions/tf_kdl.h>
#include "ros/ros.h"
#include <algorithm>
#include <numeric>
#include <LinearMath/btMatrix3x3.h>

namespace jaco_arm_kinematics
{
	JacoArmKinematics::JacoArmKinematics(): node_handle("~")
	{

		
		while (!arm_kinematics_constraint_aware::loadRobotModel(node_handle, robot_model, root_name, tip_name, xml_string) && node_handle.ok())
		{
			ROS_ERROR("Could not load robot model. Are you sure the robot model is on the parameter server?");
			ros::Duration(0.5).sleep();
		}

		ROS_INFO("Loading KDL Tree");

		if (!arm_kinematics_constraint_aware::getKDLChain(xml_string, root_name, tip_name, kdl_chain))
		{
			active_ = false;
			ROS_ERROR("Could not load kdl tree");
		}

		ROS_INFO("Advertising services");

		jnt_to_pose_solver.reset(new KDL::ChainFkSolverPos_recursive(kdl_chain));


		if (!node_handle.getParam("robot_description", robot_desc_string))
		{
			ROS_FATAL("Couldn't get a robot_description from the param server");			
		}

		while(!arm_kinematics_constraint_aware::loadRobotModel(node_handle,robot_model,root_name,tip_name,xml_string) && node_handle.ok())
		{
		    ROS_FATAL("Could not load robot model. Are you sure the robot model is on the parameter server?");
		    ros::Duration(0.5).sleep();
		}


		if (!arm_kinematics_constraint_aware::getChainInfoFromRobotModel(robot_model, root_name, tip_name, kinematic_info))
		{
			ROS_FATAL("Could not get chain info!");
		}

		fk_solver_info = kinematic_info;
		ik_solver_info = fk_solver_info;

            	for(unsigned int i = 0; i< kinematic_info.limits.size(); i++)
                	std::cerr<<"kinematic information for joint = "<< i+1 <<"    "<<kinematic_info.limits[i]<<std::endl;

		for (unsigned int i = 0; i < ik_solver_info.joint_names.size(); ++i)
		{
			ROS_INFO("JacoKinematics:: joint name: %s", ik_solver_info.joint_names[i].c_str());			
		}

		for (unsigned int i = 0; i < ik_solver_info.link_names.size(); ++i)
		{
			ROS_INFO("JacoKinematics can solve IK for %s", ik_solver_info.link_names[i].c_str());
		}

		for (unsigned int i = 0; i < fk_solver_info.link_names.size(); ++i)
		{
			ROS_INFO("JacoKinematics can solve FK for %s", fk_solver_info.link_names[i].c_str());
		}

		ROS_INFO("JacoKinematics::active");
		
		num_joints = 6;		

		active_ = true;

		ik_service                      = node_handle.advertiseService("get_ik" , 			&JacoArmKinematics::getOpenRaveIK, 		this);
		fk_service                      = node_handle.advertiseService("get_fk", 			&JacoArmKinematics::getPositionFK, 		this);
		constraint_aware_ik_service     = node_handle.advertiseService("get_constraint_aware_ik",	&JacoArmKinematics::get_constraint_aware_ik , 	this);
		ik_solver_info_service          = node_handle.advertiseService("get_ik_solver_info",		&JacoArmKinematics::getIKSolverInfo,		this);
		fk_solver_info_service          = node_handle.advertiseService("get_fk_solver_info",		&JacoArmKinematics::getFKSolverInfo,		this);

		rotmat_4_IK.setIdentity();

		
		current_jointangles.resize(6);		

		for(int i = 0; i < 6; i++)		
			current_jointangles.at(i) = 0.0;

		jt_weight[0] = 0.4;
		jt_weight[1] = 0.8;
		jt_weight[2] = 0.5;
		jt_weight[3] = 0.2;
		jt_weight[4] = 0.2;
		jt_weight[5] = 0.5;

					
		collision_models_interface_ = new planning_environment::CollisionModelsInterface("robot_description");	
					
    	}

	
  	bool JacoArmKinematics::isActive()
  	{
    		if (active_) 
		{ 
			return true; 
		}
		return false;
  	}


	bool JacoArmKinematics::getOpenRaveIK(kinematics_msgs::GetPositionIK::Request &request,
						kinematics_msgs::GetPositionIK::Response &response)
  	{
    		if (!active_)
    		{
      			ROS_ERROR("IK service not active");
      			return true;
    		}

		// !!!!!!!!!  need to use at some point !!!!
    		//if (!checkIKService(request, response, ik_solver_info_)) { return true; }


		std::vector<IKSolution> vsolutions;
    		std::vector<IKReal> sol(6);
    		IKReal eerot[9],eetrans[3];		
		
	 	btQuaternion t(request.ik_request.pose_stamped.pose.orientation.x, request.ik_request.pose_stamped.pose.orientation.y, 
						request.ik_request.pose_stamped.pose.orientation.z, request.ik_request.pose_stamped.pose.orientation.w);
   		
		rotmat_4_IK.setRotation(t);
		eerot[0] = rotmat_4_IK[0][0];	eerot[1] = rotmat_4_IK[0][1];     eerot[2] = rotmat_4_IK[0][2];	eetrans[0] = request.ik_request.pose_stamped.pose.position.x;
		eerot[3] = rotmat_4_IK[1][0];	eerot[4] = rotmat_4_IK[1][1];     eerot[5] = rotmat_4_IK[1][2];	eetrans[1] = request.ik_request.pose_stamped.pose.position.y;
		eerot[6] = rotmat_4_IK[2][0];   eerot[7] = rotmat_4_IK[2][1];     eerot[8] = rotmat_4_IK[2][2];	eetrans[2] = request.ik_request.pose_stamped.pose.position.z;

		/*std::cerr<<" Position"<<std::endl;
		for(int i = 0; i< 3;i++)
			std::cerr<<eetrans[i]<<std::endl;

		std::cerr<<" -------------"<<std::endl;
		std::cerr<<" Orientation"<<std::endl;
		std::cerr<<request.ik_request.pose_stamped.pose.orientation<<std::endl;
		std::cerr<<" ----------------"<<std::endl;*/
	
    		bool bSuccess = ik(eetrans, eerot, NULL, vsolutions);

		if( !bSuccess ) 
		{
			fprintf(stderr,"Failed to get ik solution\n");
			ROS_ERROR("OpenRaveIK found no solutions!");
			response.error_code.val = response.error_code.NO_IK_SOLUTION;
			
		}
		else
		{	
			if ((int)vsolutions.size() > 1)
			{
				printf("Found %d ik solutions:\n", (int)vsolutions.size());
				ROS_INFO("OpenRaveIK found more than one solution, we'll take the first one ");
				
				vsolutions[0].GetSolution(&sol[0],NULL);
	
				response.solution.joint_state.header.stamp    	= ros::Time::now();
				response.solution.joint_state.header.frame_id 	= root_name;//root_name;
				response.solution.joint_state.name 		= ik_solver_info.joint_names;
	
	   			response.solution.joint_state.position.resize(6);
	
				for (size_t i = 0; i < sol.size(); i++){
					response.solution.joint_state.position[i] = sol[i];
			
					ROS_INFO("ik %f", sol[i]);
				}

				 /*std::vector<IKReal> sol1(getNumJoints());
				    for(std::size_t i = 0; i < vsolutions.size(); ++i) {
					printf("sol%d (free=%d): ", (int)i, (int)vsolutions[i].GetFree().size());
					std::vector<IKReal> vsolfree(vsolutions[i].GetFree().size());
					vsolutions[i].GetSolution(&sol1[0],vsolfree.size()>0?&vsolfree[0]:NULL);
					for( std::size_t j = 0; j < sol1.size(); ++j)
					    printf("%.15f, ", sol1[j]);
					printf("\n");
				    }*/

				response.error_code.val = response.error_code.SUCCESS;

			}
		}

		return true;
	}

    bool JacoArmKinematics::get_constraint_aware_ik (kinematics_msgs::GetConstraintAwarePositionIK::Request &request,
						kinematics_msgs::GetConstraintAwarePositionIK::Response &response)
	{
		if (!active_)
    		{
      			ROS_ERROR("IK service not active");
      			return true;
    		}
		
		
		geometry_msgs::PoseStamped pose_msg_in = request.ik_request.pose_stamped;
		geometry_msgs::PoseStamped pose_msg_out;

		collision_models_interface_->resetToStartState(*collision_models_interface_->getPlanningSceneState());

  		planning_environment::setRobotStateAndComputeTransforms(request.ik_request.robot_state, *collision_models_interface_->getPlanningSceneState());

		if(!collision_models_interface_->convertPoseGivenWorldTransform(*collision_models_interface_->getPlanningSceneState(),
				                                                  root_name,
				                                                  pose_msg_in.header,
				                                                  pose_msg_in.pose,
				                                                  pose_msg_out)) 
		{
		    response.error_code.val = response.error_code.FRAME_TRANSFORM_FAILURE;
		    return true;
		}


		
		// !!!!!!!!!  need to use at some point !!!!
    		//if (!checkIKService(request, response, ik_solver_info_)) { return true; }

		for(int i = 0; i<6; i++)
			current_jointangles.at(i) = request.ik_request.ik_seed_state.joint_state.position[i+7];

		
		std::vector<IKSolution> vsolutions;
    		std::vector<IKReal> sol(6);
    		IKReal eerot[9],eetrans[3];		
		
	 	btQuaternion t(pose_msg_out.pose.orientation.x, pose_msg_out.pose.orientation.y, 
						pose_msg_out.pose.orientation.z, pose_msg_out.pose.orientation.w);
   		
		rotmat_4_IK.setRotation(t);

		eerot[0] = rotmat_4_IK[0][0];	eerot[1] = rotmat_4_IK[0][1];     eerot[2] = rotmat_4_IK[0][2];	eetrans[0] = pose_msg_out.pose.position.x;
		eerot[3] = rotmat_4_IK[1][0];	eerot[4] = rotmat_4_IK[1][1];     eerot[5] = rotmat_4_IK[1][2];	eetrans[1] = pose_msg_out.pose.position.y;
		eerot[6] = rotmat_4_IK[2][0];   eerot[7] = rotmat_4_IK[2][1];     eerot[8] = rotmat_4_IK[2][2];	eetrans[2] = pose_msg_out.pose.position.z;		

	
    		bool bSuccess = ik(eetrans, eerot, NULL, vsolutions);

		if( !bSuccess ) 
		{
			fprintf(stderr,"Failed to get ik solution\n");
			ROS_ERROR("OpenRaveIK found no solutions!");
			response.error_code.val = response.error_code.NO_IK_SOLUTION;
			
		}
		else
		{	
			
			std::vector<std::vector<float> > optSol;

			if(!pickOptimalIkSolution(current_jointangles, vsolutions, optSol))
			{
				response.error_code.val = response.error_code.JOINT_LIMITS_VIOLATED;
				ROS_ERROR("OpenRaveIK solutions violates joint limits!");
			}
        		else if ((int)vsolutions.size() > 1)
			{
				printf("Found %d ik solutions:\n", (int)vsolutions.size());
				ROS_INFO("OpenRaveIK found more than one solution, we'll take the optimal solution ");
					
	
				response.solution.joint_state.header.stamp    	= ros::Time::now();
				response.solution.joint_state.header.frame_id 	= root_name;//root_name;
				response.solution.joint_state.name 		= ik_solver_info.joint_names;
	
	   			response.solution.joint_state.position.resize(6);


				for (int i = 0; i < 6; i++)
				{
					response.solution.joint_state.position[i] = optSol.at(0).at(i);
					ROS_INFO("ik %f", optSol[0][i]);
				}


				response.error_code.val = response.error_code.SUCCESS;
			}
			else
				response.error_code.val = response.error_code.NO_IK_SOLUTION;
		}

		return true;
	}		

	bool JacoArmKinematics::getPositionFK(kinematics_msgs::GetPositionFK::Request &request, kinematics_msgs::GetPositionFK::Response &response) 
	{
	
		if (!active_)
		{
			ROS_ERROR("FK service not active");
			return true;
		}

		if (!arm_kinematics_constraint_aware::checkFKService(request, response, fk_solver_info)) { return true; }

		KDL::Frame p_out;
		KDL::JntArray jnt_pos_in;
		geometry_msgs::PoseStamped pose;
		tf::Stamped<tf::Pose> tf_pose;

		jnt_pos_in.resize(num_joints);

		for (int i = 0; i < (int) request.robot_state.joint_state.position.size(); ++i)
		{
			int tmp_index = arm_kinematics_constraint_aware::getJointIndex(request.robot_state.joint_state.name[i], fk_solver_info);
			if (tmp_index >=0) 
			{ 
				jnt_pos_in(tmp_index) = request.robot_state.joint_state.position[i]; 
			}
		}

		response.pose_stamped.resize(request.fk_link_names.size());
		response.fk_link_names.resize(request.fk_link_names.size());

		bool valid = true;

		for (unsigned int i = 0; i < request.fk_link_names.size(); ++i)
		{
			ROS_DEBUG("End effector index: %d", arm_kinematics_constraint_aware::getKDLSegmentIndex(kdl_chain, request.fk_link_names[i]));
			ROS_DEBUG("Chain indices: %d", kdl_chain.getNrOfSegments());

			if (jnt_to_pose_solver->JntToCart(jnt_pos_in, p_out, arm_kinematics_constraint_aware::getKDLSegmentIndex(kdl_chain, request.fk_link_names[i])) >= 0)
			{
				tf_pose.frame_id_ = root_name;
				tf_pose.stamp_ = ros::Time();
				tf::PoseKDLToTF(p_out, tf_pose);

				try
				{
					tf.transformPose(request.header.frame_id, tf_pose, tf_pose);
				}
				catch (...)
				{
					ROS_ERROR("Could not transform FK pose to frame: %s", request.header.frame_id.c_str());
					response.error_code.val = response.error_code.FRAME_TRANSFORM_FAILURE;
					return false;
				}

				tf::poseStampedTFToMsg(tf_pose,pose);
				response.pose_stamped[i] = pose;
				response.fk_link_names[i] = request.fk_link_names[i];
				response.error_code.val = response.error_code.SUCCESS;
			}
			else
			{
				ROS_ERROR("Could not compute FK for %s", request.fk_link_names[i].c_str());
				response.error_code.val = response.error_code.NO_FK_SOLUTION;
				valid = false;
			}
		}
        	return valid;
	}

	bool JacoArmKinematics::getIKSolverInfo(kinematics_msgs::GetKinematicSolverInfo::Request &request, 
		       					kinematics_msgs::GetKinematicSolverInfo::Response &response)
	{
		if(!active_)
		{
			ROS_ERROR("Jaco Kinematic node is not active");
			return true;
		}
		response.kinematic_solver_info = ik_solver_info;
		return true;
	}

	bool JacoArmKinematics::getFKSolverInfo(kinematics_msgs::GetKinematicSolverInfo::Request &request, 
		       					kinematics_msgs::GetKinematicSolverInfo::Response &response)
	{
		if(!active_)
		{
			ROS_ERROR("Jaco Kinematic node is not active");
			return true;
		}
		response.kinematic_solver_info = fk_solver_info;
		return true;
	}
    
	bool JacoArmKinematics::pickOptimalIkSolution(const std::vector<float> &cur_jtang, const std::vector<IKSolution> &redundantSolutions, std::vector<std::vector<float> > &optSol)
	{

		int num_solution = redundantSolutions.size();
		std::vector<IKReal> sol(6);
		std::vector<std::vector<float> > all_solution, refind_solutions;
		std::vector<int> joint_limit_exceed_solution_index;
		std::vector<float> del_sol;
		int jt_limit_exceed_sol_ct = 0;
		int ct = 0;
		std::vector<redundant_ik_solution> refined_ik_sol_container;
		int error_code;

		all_solution.resize(num_solution, std::vector<float> (6,0.0) );
		joint_limit_exceed_solution_index.resize(num_solution, 0);

		std::cerr << "-----  Current Joint angle --------"<<std::endl;
		for(int j = 0; j < 6; ++j)
			std::cerr <<cur_jtang.at(j)<<"    ";
		std::cerr<<std::endl;

		std::cerr << "-----  Actual solution --------"<<std::endl;

		for(int i = 0; i < num_solution; ++i)
		{
		
			std::vector<IKReal> vsolfree(redundantSolutions[i].GetFree().size());

			redundantSolutions[i].GetSolution(&sol[0],vsolfree.size()>0?&vsolfree[0]:NULL);

		        collisionCheck(sol, error_code);

		        if(	(!(((sol[0]>=kinematic_info.limits[0].min_position) && (sol[0]<=kinematic_info.limits[0].max_position)) &&
		                ((sol[1]>=kinematic_info.limits[1].min_position) && (sol[1]<=kinematic_info.limits[1].max_position)) &&
		                ((sol[2]>=kinematic_info.limits[2].min_position) && (sol[2]<=kinematic_info.limits[2].max_position)) &&
		                ((sol[3]>=kinematic_info.limits[3].min_position) && (sol[3]<=kinematic_info.limits[3].max_position)) &&
		                ((sol[4]>=kinematic_info.limits[4].min_position) && (sol[4]<=kinematic_info.limits[4].max_position)) &&
		                ((sol[5]>=kinematic_info.limits[5].min_position) && (sol[5]<=kinematic_info.limits[5].max_position)) )) ||
		                (error_code != kinematics::SUCCESS) )
			{
				joint_limit_exceed_solution_index.at(i) = 1;
		                jt_limit_exceed_sol_ct++;
			}

			for( std::size_t j = 0; j < 6; ++j)			
			{
				all_solution[i][j] = sol[j];
				std::cerr <<sol[j]<<"    ";
		        }
		        std::cerr<<std::endl;
		}

		if(jt_limit_exceed_sol_ct==num_solution)
		{
		        std::cerr << "-----  joint_limit_exceed_solution_index --------"<<std::endl;
		        for(int j = 0; j < num_solution; ++j)
		        std::cerr <<joint_limit_exceed_solution_index.at(j)<<"    ";

		        std::cerr<<std::endl;
		        return false;
		}

		// variables for holding the refined IK solution after checking joint limits and collision
		del_sol.resize(num_solution - jt_limit_exceed_sol_ct, 0.0);                                     // variable for weighted ik sol based on min joint movement
		refind_solutions.resize(num_solution - jt_limit_exceed_sol_ct, std::vector<float> (6,0.0) );    // variable for holding the refind ik solution
		optSol.resize(num_solution - jt_limit_exceed_sol_ct, std::vector<float> (6,0.0) );              // optimal solution - sorted ik solution based weighted ik sol
		redundant_ik_solution refined_ik_sol[num_solution - jt_limit_exceed_sol_ct];                    // variable used to sorting the ik solution
		refined_ik_sol_container.resize(num_solution - jt_limit_exceed_sol_ct);                         // variable used to sorting the ik solution

		for(int i = 0; i < num_solution; ++i)
		{
			if (joint_limit_exceed_solution_index.at(i)==0)
			{
				for(int j = 0; j < 6; ++j)
				{
					del_sol.at(ct) 		= del_sol.at(ct) + ( jt_weight[j] *fabs( cur_jtang[j] - all_solution[i][j] ));
					refind_solutions[ct][j] 	= all_solution[i][j];
				}
				refined_ik_sol[ct].opt_iksol_index_value = del_sol.at(ct);
				refined_ik_sol[ct].ik_sol		 = refind_solutions[ct];
				ct++;
			}
		}

		for(int i = 0; i<num_solution - jt_limit_exceed_sol_ct; i++)
		        refined_ik_sol_container.at(i) = refined_ik_sol[i];


		std::sort(refined_ik_sol_container.begin(), refined_ik_sol_container.end(), compare_result());



		for(int i = 0; i<num_solution - jt_limit_exceed_sol_ct; i++)
		{
		        for(int j = 0; j < 6; ++j)
		        optSol.at(i).at(j) = refined_ik_sol_container.at(i).ik_sol.at(j);
		}


		/*
		std::cerr << "-----  del jt --------"<<std::endl;
		for(int j = 0; j < num_solution - jt_limit_exceed_sol_ct; ++j)
		        std::cerr <<del_sol.at(j)<<"    ";
		std::cerr<<std::endl;

		std::cerr << "-----  refind_sol --------"<<std::endl;
		for(int j = 0; j < num_solution - jt_limit_exceed_sol_ct; ++j)
		{
		        for(int i = 0; i < 6; ++i)
		        std::cerr <<refind_solutions.at(j).at(i)<<"    ";
		        std::cerr<<std::endl;
		}

		//std::cerr << "-----  opt_sol --------"<<std::endl;
		//for(int j = 0; j < 6; ++j)
		//	std::cerr <<optSol.at(j)<<"    ";
		//std::cerr<<std::endl;
		//std::cerr << "------------------"<<std::endl;

		std::cerr << "-----  new opt_sol --------"<<std::endl;
		for(unsigned int i = 0; i < refined_ik_sol_container.size(); i++)
		{
		        for(int j = 0; j < 6; ++j)
		        std::cerr <<refined_ik_sol_container.at(i).ik_sol.at(j)<<"    ";
		        std::cerr<<std::endl;
		}
		std::cerr << "------------------"<<std::endl;*/
	
		return true;
	}

        void JacoArmKinematics::collisionCheck(const std::vector<IKReal> &ik_solution, int &error_code)
        {
                std::map<std::string, double> joint_values;

                for(unsigned int i=0; i < kinematic_info.joint_names.size(); i++)
                joint_values[kinematic_info.joint_names[i]] = ik_solution[i];


                state_=collision_models_interface_->getPlanningSceneState();

                state_->setKinematicState(joint_values);

                error_code = kinematics::SUCCESS;

                if(collision_models_interface_->isKinematicStateInSelfCollision(*state_))
                        error_code = kinematics::IK_LINK_IN_COLLISION; // self collision
                else if(collision_models_interface_->isKinematicStateInCollision(*state_))
                        error_code = kinematics::STATE_IN_COLLISION;
                else if(!planning_environment::doesKinematicStateObeyConstraints(*state_, constraints_))
                        error_code = kinematics::GOAL_CONSTRAINTS_VIOLATED;

        }
	
}




