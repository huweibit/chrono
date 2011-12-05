#include "ChSystemGPU.h"
#include <omp.h>

namespace chrono {

ChSystemGPU::ChSystemGPU(unsigned int max_objects) :
	ChSystem(1000, 10000, false) {
	num_gpu = 1;
	gpu_data_manager = new ChGPUDataManager();
	counter = 0;
	max_obj = max_objects;

	LCP_descriptor = new ChLcpSystemDescriptorGPU();
	contact_container = new ChContactContainerGPUsimple();
	collision_system = new ChCollisionSystemGPU();
	LCP_solver_speed = new ChLcpIterativeSolverGPUsimple((ChContactContainerGPUsimple*) contact_container);
	((ChLcpIterativeSolverGPUsimple*) (LCP_solver_speed))->data_container = gpu_data_manager;
	((ChCollisionSystemGPU*) (collision_system))->data_container = gpu_data_manager;

}

int ChSystemGPU::Integrate_Y_impulse_Anitescu() {

	mtimer_step.start();
	this->stepcount++;
	Setup();
	Update();
	gpu_data_manager->HostToDevice();
	ComputeCollisions();
	SolveSystem();
	gpu_data_manager->DeviceToHost();

#pragma omp parallel for
	for (int i = 0; i < bodylist.size(); i++) {
		ChBodyGPU* mbody = (ChBodyGPU*) bodylist[i];
		if (mbody->IsActive()) {
			mbody->SetPos(CHVECCAST(gpu_data_manager->host_pos_data[i]));
			mbody->SetRot(CHQUATCAST(gpu_data_manager->host_rot_data[i]));
			mbody->SetPos_dt(CHVECCAST(gpu_data_manager->host_vel_data[i]));
			mbody->SetPos_dtdt(CHVECCAST(gpu_data_manager->host_acc_data[i]));
			mbody->SetWvel_loc(CHVECCAST(gpu_data_manager->host_omg_data[i]));
			mbody->SetAppliedForce(CHVECCAST(gpu_data_manager->host_fap_data[i]));
		}
	}

	// updates the reactions of the constraint
	LCPresult_Li_into_reactions(1.0 / this->GetStep()); // R = l/dt  , approximately
	timer_lcp = mtimer_lcp();
	timer_collision_broad = mtimer_cd();
	ChTime += GetStep();
	mtimer_step.stop();
	timer_step = mtimer_step();// Time elapsed for step..
	return 1;
}

double ChSystemGPU::ComputeCollisions() {
	mtimer_cd.start();

	float3 bin_size_vec;
	float max_dimension;
	float collision_envelope = 0;
	uint number_of_contacts_possible;
	ChCollisionSystemGPU* collision_system_gpu = ((ChCollisionSystemGPU*) (collision_system));
	collision_system_gpu->ComputeAABB(
										gpu_data_manager->number_of_models,
										gpu_data_manager->gpu_data.device_pos_data,
										gpu_data_manager->gpu_data.device_rot_data,
										gpu_data_manager->gpu_data.device_ObA_data,
										gpu_data_manager->gpu_data.device_ObB_data,
										gpu_data_manager->gpu_data.device_ObC_data,
										gpu_data_manager->gpu_data.device_ObR_data,
										gpu_data_manager->gpu_data.device_typ_data,
										gpu_data_manager->gpu_data.device_aabb_data);
	collision_system_gpu->ComputeBounds(
										gpu_data_manager->number_of_models,
										gpu_data_manager->gpu_data.device_aabb_data,
										gpu_data_manager->min_bounding_point,
										gpu_data_manager->max_bounding_point);
	collision_system_gpu->ComputeUpdateAABB(
											gpu_data_manager->gpu_data.device_aabb_data,
											gpu_data_manager->min_bounding_point,
											gpu_data_manager->max_bounding_point,
											bin_size_vec,
											max_dimension,
											collision_envelope,
											gpu_data_manager->number_of_models);
	collision_system_gpu->ComputeBroadPhase(
											gpu_data_manager->gpu_data.device_aabb_data,
											gpu_data_manager->gpu_data.device_fam_data,
											gpu_data_manager->gpu_data.device_typ_data,
											gpu_data_manager->gpu_data.contact_pair,
											gpu_data_manager->number_of_models,
											bin_size_vec,
											number_of_contacts_possible);
	collision_system_gpu->ComputeNarrowPhase(
												gpu_data_manager->gpu_data.device_norm_data,
												gpu_data_manager->gpu_data.device_cpta_data,
												gpu_data_manager->gpu_data.device_cptb_data,
												gpu_data_manager->gpu_data.device_dpth_data,
												gpu_data_manager->gpu_data.device_bids_data,
												gpu_data_manager->gpu_data.device_gam_data,
												gpu_data_manager->gpu_data.device_pos_data,
												gpu_data_manager->gpu_data.device_rot_data,
												gpu_data_manager->gpu_data.device_ObA_data,
												gpu_data_manager->gpu_data.device_ObB_data,
												gpu_data_manager->gpu_data.device_ObC_data,
												gpu_data_manager->gpu_data.device_ObR_data,
												gpu_data_manager->gpu_data.device_typ_data,
												gpu_data_manager->gpu_data.contact_pair,
												number_of_contacts_possible,
												gpu_data_manager->number_of_contacts);
	this->ncontacts = gpu_data_manager->number_of_contacts;
	mtimer_cd.stop();
	return 0;
}

double ChSystemGPU::SolveSystem() {
	mtimer_lcp.start();
	((ChLcpIterativeSolverGPUsimple*) (LCP_solver_speed))->SolveSys(
																	gpu_data_manager->number_of_objects,
																	gpu_data_manager->number_of_bilaterals,
																	gpu_data_manager->number_of_contacts,
																	gpu_data_manager->gpu_data.device_norm_data,
																	gpu_data_manager->gpu_data.device_cpta_data,
																	gpu_data_manager->gpu_data.device_cptb_data,
																	gpu_data_manager->gpu_data.device_dpth_data,
																	gpu_data_manager->gpu_data.device_bids_data,
																	gpu_data_manager->gpu_data.device_bilateral_data,
																	gpu_data_manager->gpu_data.device_pos_data,
																	gpu_data_manager->gpu_data.device_rot_data,
																	gpu_data_manager->gpu_data.device_vel_data,
																	gpu_data_manager->gpu_data.device_omg_data,
																	gpu_data_manager->gpu_data.device_acc_data,
																	gpu_data_manager->gpu_data.device_inr_data,
																	gpu_data_manager->gpu_data.device_gyr_data,
																	gpu_data_manager->gpu_data.device_frc_data,
																	gpu_data_manager->gpu_data.device_trq_data,
																	gpu_data_manager->gpu_data.device_fap_data,
																	gpu_data_manager->gpu_data.device_gam_data,
																	gpu_data_manager->gpu_data.device_aux_data,
																	gpu_data_manager->gpu_data.device_ObA_data,
																	gpu_data_manager->gpu_data.device_ObB_data,
																	gpu_data_manager->gpu_data.device_ObC_data,
																	gpu_data_manager->gpu_data.device_ObR_data,
																	gpu_data_manager->gpu_data.device_typ_data,
																	gpu_data_manager->gpu_data.device_lim_data);
	((ChContactContainerGPUsimple*) this->contact_container)->SetNcontacts(gpu_data_manager->number_of_contacts);
	mtimer_lcp.stop();
	return 0;
}
void ChSystemGPU::AddBody(ChSharedPtr<ChBodyGPU> newbody) {
	newbody->AddRef();
	newbody->SetSystem(this);
	bodylist.push_back((newbody).get_ptr());

	ChBodyGPU* gpubody = ((ChBodyGPU*) newbody.get_ptr());
	gpubody->id = counter;
	if (newbody->GetCollide()) newbody->AddCollisionModelsToSystem();

	ChLcpVariablesBodyOwnMass* mbodyvar = &(newbody->Variables());

	float inv_mass = (1.0) / (mbodyvar->GetBodyMass());
	newbody->GetRot().Normalize();
	gpu_data_manager->host_vel_data.push_back(F3(
													mbodyvar->Get_qb().GetElementN(0),
													mbodyvar->Get_qb().GetElementN(1),
													mbodyvar->Get_qb().GetElementN(2)));
	gpu_data_manager->host_omg_data.push_back(F3(
													mbodyvar->Get_qb().GetElementN(3),
													mbodyvar->Get_qb().GetElementN(4),
													mbodyvar->Get_qb().GetElementN(5)));
	gpu_data_manager->host_pos_data.push_back(F3(newbody->GetPos().x, newbody->GetPos().y, newbody->GetPos().z));
	gpu_data_manager->host_rot_data.push_back(F4(newbody->GetRot().e0, newbody->GetRot().e1, newbody->GetRot().e2, newbody->GetRot().e3));
	gpu_data_manager->host_inr_data.push_back(F3(
													mbodyvar->GetBodyInvInertia().GetElement(0, 0),
													mbodyvar->GetBodyInvInertia().GetElement(1, 1),
													mbodyvar->GetBodyInvInertia().GetElement(2, 2)));
	gpu_data_manager->host_frc_data.push_back(F3(mbodyvar->Get_fb().ElementN(0), mbodyvar->Get_fb().ElementN(1), mbodyvar->Get_fb().ElementN(2))); //forces
	gpu_data_manager->host_trq_data.push_back(F3(mbodyvar->Get_fb().ElementN(3), mbodyvar->Get_fb().ElementN(4), mbodyvar->Get_fb().ElementN(5))); //torques
	gpu_data_manager->host_aux_data.push_back(F3(newbody->IsActive(), newbody->GetKfriction(), inv_mass));
	gpu_data_manager->host_lim_data.push_back(F3(newbody->GetLimitSpeed(), newbody->GetMaxSpeed(), newbody->GetMaxWvel()));

	counter++;
	gpu_data_manager->number_of_objects = counter;
	if (counter % 1000 == 0) {
		cout << "Added: " << counter << " objects" << endl;
	}
}

void ChSystemGPU::RemoveBody(ChSharedPtr<ChBodyGPU> mbody) {
	assert(std::find<std::vector<ChBody*>::iterator>(bodylist.begin(), bodylist.end(), mbody.get_ptr()) != bodylist.end());

	// remove from collision system
	if (mbody->GetCollide()) mbody->RemoveCollisionModelsFromSystem();

	// warning! linear time search, to erase pointer from container.
	bodylist.erase(std::find<std::vector<ChBody*>::iterator>(bodylist.begin(), bodylist.end(), mbody.get_ptr()));

	// nullify backward link to system
	mbody->SetSystem(0);
	// this may delete the body, if none else's still referencing it..
	mbody->RemoveRef();
}

void ChSystemGPU::Update() {
	ChTimer<double> mtimer;
	mtimer.start(); // Timer for profiling
#pragma omp parallel for
	for (int i = 0; i < bodylist.size(); i++) // Updates recursively all other aux.vars
	{
		bodylist[i]->UpdateTime(ChTime);
		bodylist[i]->UpdateMarkers(ChTime);
		bodylist[i]->UpdateForces(ChTime);
		bodylist[i]->VariablesFbReset();
		bodylist[i]->VariablesFbLoadForces(GetStep());
		bodylist[i]->VariablesQbLoadSpeed();

		ChLcpVariablesBodyOwnMass* mbodyvar = &(bodylist[i]->Variables());
		gpu_data_manager->host_vel_data[i] = (F3(bodylist[i]->GetPos_dt().x, bodylist[i]->GetPos_dt().y, bodylist[i]->GetPos_dt().z));
		gpu_data_manager->host_omg_data[i] = (F3(bodylist[i]->GetWvel_loc().x, bodylist[i]->GetWvel_loc().y, bodylist[i]->GetWvel_loc().z));
		gpu_data_manager->host_pos_data[i] = (F3(bodylist[i]->GetPos().x, bodylist[i]->GetPos().y, bodylist[i]->GetPos().z));
		gpu_data_manager->host_rot_data[i] = (F4(
													bodylist[i]->GetRot().e0,
													bodylist[i]->GetRot().e1,
													bodylist[i]->GetRot().e2,
													bodylist[i]->GetRot().e3));
		gpu_data_manager->host_inr_data[i] = (F3(
													mbodyvar->GetBodyInvInertia().GetElement(0, 0),
													mbodyvar->GetBodyInvInertia().GetElement(1, 1),
													mbodyvar->GetBodyInvInertia().GetElement(2, 2)));
		gpu_data_manager->host_frc_data[i] = (F3(mbodyvar->Get_fb().ElementN(0), mbodyvar->Get_fb().ElementN(1), mbodyvar->Get_fb().ElementN(2))); //forces
		gpu_data_manager->host_trq_data[i] = (F3(mbodyvar->Get_fb().ElementN(3), mbodyvar->Get_fb().ElementN(4), mbodyvar->Get_fb().ElementN(5))); //torques
		gpu_data_manager->host_aux_data[i] = (F3(bodylist[i]->IsActive(), bodylist[i]->GetKfriction(), 1.0f / mbodyvar->GetBodyMass()));
		gpu_data_manager->host_lim_data[i] = (F3(bodylist[i]->GetLimitSpeed(), bodylist[i]->GetMaxSpeed(), bodylist[i]->GetMaxWvel()));

	}
	std::list<ChLink*>::iterator it;
	unsigned int number_of_bilaterals = 0;
	uint counter = 0;
	for (it = linklist.begin(); it != linklist.end(); it++) {
		(*it)->Update(ChTime);
		if ((*it)->IsActive()) {
			number_of_bilaterals++;
		}
	}
	gpu_data_manager->number_of_bilaterals = number_of_bilaterals;
	gpu_data_manager->host_bilateral_data.resize(number_of_bilaterals * CH_BILATERAL_VSIZE);
	for (it = linklist.begin(); it != linklist.end(); it++) {
		(*it)->ConstraintsBiReset();
	}
	for (it = linklist.begin(); it != linklist.end(); it++) {
		(*it)->ConstraintsBiLoad_C(1 / GetStep(), max_penetration_recovery_speed, true);
		(*it)->ConstraintsBiLoad_Ct(1);
		(*it)->ConstraintsFbLoadForces(GetStep());
		(*it)->ConstraintsLoadJacobians();
		if ((*it)->IsActive()) {
			ChLcpConstraintTwoBodies* mbilateral = (ChLcpConstraintTwoBodies*) ((*it));
			// Update auxiliary data in all constraints before starting, that is: g_i=[Cq_i]*[invM_i]*[Cq_i]' and  [Eq_i]=[invM_i]*[Cq_i]'
			mbilateral->Update_auxiliary();//***NOTE*** not efficient here - can be on GPU, and [Eq_i] not needed
			int idA = ((ChBodyGPU*) ((ChLcpVariablesBody*) (mbilateral->GetVariables_a()))->GetUserData())->id;
			int idB = ((ChBodyGPU*) ((ChLcpVariablesBody*) (mbilateral->GetVariables_b()))->GetUserData())->id;

			float4 A, B, C, D;
			A = F4(mbilateral->Get_Cq_a()->GetElementN(0), mbilateral->Get_Cq_a()->GetElementN(1), mbilateral->Get_Cq_a()->GetElementN(2), idA);//J1x
			B = F4(mbilateral->Get_Cq_b()->GetElementN(0), mbilateral->Get_Cq_b()->GetElementN(1), mbilateral->Get_Cq_b()->GetElementN(2), idB);//J2x
			C = F4(mbilateral->Get_Cq_a()->GetElementN(3), mbilateral->Get_Cq_a()->GetElementN(4), mbilateral->Get_Cq_a()->GetElementN(5), 0);//J1w
			D = F4(mbilateral->Get_Cq_b()->GetElementN(3), mbilateral->Get_Cq_b()->GetElementN(4), mbilateral->Get_Cq_b()->GetElementN(5), 0);//J2w
			bool isUni = (mbilateral->IsUnilateral()) ? 1 : 0;
			gpu_data_manager->host_bilateral_data[counter + number_of_bilaterals * 0] = A;
			gpu_data_manager->host_bilateral_data[counter + number_of_bilaterals * 1] = B;
			gpu_data_manager->host_bilateral_data[counter + number_of_bilaterals * 2] = C;
			gpu_data_manager->host_bilateral_data[counter + number_of_bilaterals * 3] = D;
			gpu_data_manager->host_bilateral_data[counter + number_of_bilaterals * 4] = F4(
																							(1.0 / mbilateral->Get_g_i()),
																							mbilateral->Get_b_i(),
																							0,
																							isUni);
			counter++;

		}
	}

	mtimer.stop();
	timer_update += mtimer();

}

void ChSystemGPU::ChangeLcpSolverSpeed(ChLcpSolver* newsolver) {
	assert(newsolver);
	if (this->LCP_solver_speed) delete (this->LCP_solver_speed);
	this->LCP_solver_speed = newsolver;

	((ChLcpIterativeSolverGPUsimple*) (LCP_solver_speed))->data_container = gpu_data_manager;
}

void ChSystemGPU::ChangeCollisionSystem(ChCollisionSystem* newcollsystem) {
	assert(this->GetNbodies() == 0);
	assert(newcollsystem);
	if (this->collision_system) delete (this->collision_system);
	this->collision_system = newcollsystem;

	((ChCollisionSystemGPU*) (collision_system))->data_container = gpu_data_manager;
}

}
