#include "motion_matching.h"
#include "core/string/print_string.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/animation/animation_player.h"
#include "core/config/engine.h"
//#include "debug_draw_3d/debug_draw_3d.h"
#include "core/io/resource_saver.h"
#include "scene/3d/physics_body_3d.h"

NodePath MotionMatching::get_skeleton_node_path() const {
    return skeleton_node_path;
}

void MotionMatching::set_skeleton_node_path(const NodePath& np) {
    skeleton_node_path = np;
}

NodePath MotionMatching::get_animation_player_path() const {
    return animation_player_path;
}

void MotionMatching::set_animation_player_path(const NodePath& np) {
    animation_player_path = np;
}

Array MotionMatching::get_bones_to_track() const {
    return bones_to_track;
}

void MotionMatching::set_bones_to_track(const Array& array) {
    bones_to_track = array;
}

float MotionMatching::get_responsivity() const {
    return responsivity;
}

void MotionMatching::set_responsivity(float new_responsivity) {
    responsivity = new_responsivity;
    
    // kd_tree.distance_func_class.responsivity = responsivity;
}

float MotionMatching::get_speed_scale() const {
    return speed_scale;
}

void MotionMatching::set_speed_scale(float new_speed_scale) {
    speed_scale = new_speed_scale;
}

void MotionMatching::_bind_methods() {

    ClassDB::bind_method(D_METHOD("get_skeleton_node_path"), &MotionMatching::get_skeleton_node_path);
    ClassDB::bind_method(D_METHOD("set_skeleton_node_path", "skeleton_node_path"), &MotionMatching::set_skeleton_node_path);
    ClassDB::bind_method(D_METHOD("get_animation_player_path"), &MotionMatching::get_animation_player_path);
    ClassDB::bind_method(D_METHOD("set_animation_player_path", "animation_player_path"), &MotionMatching::set_animation_player_path);
    ClassDB::bind_method(D_METHOD("get_bones_to_track"), &MotionMatching::get_bones_to_track);
    ClassDB::bind_method(D_METHOD("set_bones_to_track", "bones_to_track"), &MotionMatching::set_bones_to_track);
    ClassDB::bind_method(D_METHOD("get_responsivity"), &MotionMatching::get_responsivity);
    ClassDB::bind_method(D_METHOD("set_responsivity", "new_responsivity"), &MotionMatching::set_responsivity);
    ClassDB::bind_method(D_METHOD("get_speed_scale"), &MotionMatching::get_speed_scale);
    ClassDB::bind_method(D_METHOD("set_speed_scale", "speed_scale"), &MotionMatching::set_speed_scale);
    ClassDB::bind_method(D_METHOD("process_animation"), &MotionMatching::process_animation);
    ClassDB::bind_method(D_METHOD("anim_update", "delta"), &MotionMatching::anim_update);
    ClassDB::bind_method(D_METHOD("set_goal", "trajectory"), &MotionMatching::set_goal);
    ClassDB::bind_method(D_METHOD("flip_flop", "animation", "animation_position"), &MotionMatching::flip_flop);
    
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "skeleton_node_path"), "set_skeleton_node_path", "get_skeleton_node_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "animation_player_path"), "set_animation_player_path", "get_animation_player_path");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bones_to_track"), "set_bones_to_track", "get_bones_to_track");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "responsivity", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_responsivity", "get_responsivity");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_scale", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater"), "set_speed_scale", "get_speed_scale");

    ADD_SIGNAL(MethodInfo("blend_complete"));
}

MotionMatching::MotionMatching() {
    
}

MotionMatching::~MotionMatching() {
    if(standard_deviations)
        delete [] standard_deviations;
}

void MotionMatching::process_animation() {
    skeleton = Object::cast_to<Skeleton3D>(get_node(skeleton_node_path));
    animation_player = Object::cast_to<AnimationPlayer>(get_node(animation_player_path));
    ERR_FAIL_NULL_MSG(skeleton, "Unable to get skeleton!");
    ERR_FAIL_NULL_MSG(animation_player, "Unable to get animation player!");
    // Reset
    animation_index = -1;
    animation_array.clear();
    cur_anim_time = 0.f;
    
    motion_matching::AnimData::bone_cnt = skeleton->get_bone_count();
    offset_pose_array.resize(motion_matching::AnimData::bone_cnt);
    generate_skeleton_map();
    initialize_bones_to_track();
    motion_matching::AnimData::dimension = 4 + (motion_matching::FeatureVector::trajectory_point_cnt + bones_to_track.size()) * 6;
    int bones_to_track_cnt = bones_to_track.size();
    
    List<StringName> animation_list;
    animation_player->get_animation_list(&animation_list);
    for(auto iter = animation_list.front(); iter; iter = iter->next()) {

        auto animation = animation_player->get_animation(iter->get());
        auto animation_length = animation->get_length();
        animation_length -= time_delta;
        if(animation_length < anim_blend_time + sample_step) continue;
        print_line("Processing: " + iter->get());
        for(float key_time = sample_step; key_time < animation_length; key_time += sample_step) {
            Vector3 pose_loc, pose_scale;
            Quaternion pose_quat;
            motion_matching::AnimData anim_data;
            motion_matching::FeatureVector feature_vector;
            anim_data.pose_vector.anim_name = iter->get();
            anim_data.pose_vector.anim_time = key_time;
            if(bones_to_track_cnt > 0) {
                feature_vector.joint_data.resize(bones_to_track_cnt);
            }
            int track_cnt = animation->get_track_count();
            for(int track_index = 0; track_index < track_cnt; ++track_index) {
                if(animation->track_get_type(track_index) == Animation::TYPE_POSITION_3D) {
                    pose_loc = animation->position_track_interpolate(track_index, key_time);
                    //continue;
				} else if (animation->track_get_type(track_index) == Animation::TYPE_ROTATION_3D) {
                    pose_quat = animation->rotation_track_interpolate(track_index, key_time);
				} else if (animation->track_get_type(track_index) == Animation::TYPE_SCALE_3D) {
                    pose_scale = animation->scale_track_interpolate(track_index, key_time);
                }
				//WARN_PRINT("Track bone:" + animation->track_get_path(track_index).get_subname(0));

				if(String(animation->track_get_path(track_index).get_subname(0)) != "Root") {
                    if(bones_to_track_cnt > 0) {
                        set_joint_pose_track(animation, key_time, track_index); // Set bone pose to get global transform for joint matching anim_data calculation
                        int index = -1;
                        for(auto i = 0; i != bones_to_track.size(); ++i) {
                            String name = bones_to_track[i];
                            if(name == animation->track_get_path(track_index).get_subname(0)) {
                                index = i;
                                break;
                            }
                        }
                        if(index != -1) {
                            feature_vector.joint_data.ptrw()[index].track_index = track_index;
                        }
                    }
                }
                else { // Root bone
                    // Get future trajectory points
                    // Calculate each point's veloctiy
					//WARN_PRINT("Root bone:" + animation->track_get_path(track_index));
					if(animation->track_get_type(track_index) == Animation::TYPE_POSITION_3D) {
                        for(auto i = 0; i < motion_matching::FeatureVector::trajectory_point_cnt; ++i) {
                            auto next_key_time = key_time + motion_matching::FeatureVector::intervals[i]; 
                            Vector3 future_loc = animation->position_track_interpolate(track_index, next_key_time);
                            Vector3 future_loc_delta = animation->position_track_interpolate(track_index, next_key_time + time_delta);
                            future_loc -= pose_loc; // offset from root bone
                            future_loc_delta = (future_loc_delta - pose_loc - future_loc).normalized(); // direction

                            feature_vector.trajectory[i].offset = std::forward<Vector3>(future_loc);
                            feature_vector.trajectory[i].direction = std::move(future_loc_delta);
                        }
						Vector3 pose_loc_delta = animation->position_track_interpolate(track_index, key_time + time_delta);
						feature_vector.root_velocity = (pose_loc_delta - pose_loc) / time_delta;

					} else if (animation->track_get_type(track_index) == Animation::TYPE_ROTATION_3D) {
						for(auto i = 0; i < motion_matching::FeatureVector::trajectory_point_cnt; ++i) {
                            auto next_key_time = key_time + motion_matching::FeatureVector::intervals[i];
							//WARN_PRINT("animation->track_get_type(track_index)" + animation->track_get_type(track_index));
							Quaternion future_quat = animation->rotation_track_interpolate(track_index, next_key_time);
							Quaternion future_quat_delta = animation->rotation_track_interpolate(track_index, next_key_time + time_delta);
							future_quat -= pose_quat; // offset from root bone
							future_quat_delta = (future_quat_delta - pose_quat - future_quat).normalized(); // direction

							//feature_vector.trajectory[i].offset = std::forward<Quaternion>(future_quat);
							//feature_vector.trajectory[i].direction = std::move(future_quat_delta);
                        }
						Quaternion pose_quat_delta = animation->rotation_track_interpolate(track_index, key_time + time_delta);
						// Calculate rotation around y-axis
						// Calculate rotational velocity
						feature_vector.quat = pose_quat;
						feature_vector.y_rotation = MMMath::get_y_rotation(feature_vector.quat);
                        float angle_delta = MMMath::get_y_rotation(pose_quat_delta);
                        feature_vector.y_rotational_velocity = -MMMath::delta_angle(angle_delta, feature_vector.y_rotation) / time_delta;
                        feature_vector.y_rotation_projection = feature_vector.y_rotation / (Math_TAU);

					} else if (animation->track_get_type(track_index) == Animation::TYPE_SCALE_3D) {
						for (auto i = 0; i < motion_matching::FeatureVector::trajectory_point_cnt; ++i) {
							auto next_key_time = key_time + motion_matching::FeatureVector::intervals[i];
							Vector3 future_scale = animation->scale_track_interpolate(track_index, next_key_time);
							Vector3 future_scale_delta = animation->scale_track_interpolate(track_index, next_key_time + time_delta);
							feature_vector.trajectory[i].offset = std::forward<Vector3>(future_scale);
							feature_vector.trajectory[i].direction = std::move(future_scale_delta);
						}
						Vector3 pose_scale_delta = animation->scale_track_interpolate(track_index, key_time + time_delta);
					}
					anim_data.pose_vector.root_bone_track_index = track_index;
                } // root bone track
            }

            // Calculate joint matching anim_data
            if(bones_to_track_cnt > 0) {
                for(auto i = 0; i < bones_to_track_cnt; ++i) {
                    String bone_name = bones_to_track[i];
                    auto bone_id = motion_matching::AnimData::bone_name_id_map.find(bone_name)->value;
					auto pose = skeleton->get_bone_global_pose_no_override(bone_id);
                    auto temp_jd = feature_vector.joint_data[i];
                    temp_jd.position = std::forward<Vector3>(pose.origin);
                    feature_vector.joint_data.set(i, temp_jd);
                }
                // Set skelton tranfrom in key_time + time_delta for velocity calculation
                int track_cnt = animation->get_track_count();
                for(int track_index = 0; track_index != track_cnt; ++track_index) {
                    if(String(animation->track_get_path(track_index).get_subname(0)) != "Root")
                        set_joint_pose_track(animation, key_time + time_delta, track_index);
                }
                // Calculate velocity
                for(auto i = 0; i < bones_to_track_cnt; ++i) { 
                    String bone_name = bones_to_track[i];
                    auto pair = motion_matching::AnimData::bone_name_id_map.find(bone_name);
                    auto pose = skeleton->get_bone_global_pose(pair->value);
                    Vector3 position = pose.origin;
                    position -= feature_vector.joint_data[i].position;
                    auto temp_jd = feature_vector.joint_data[i];
                    temp_jd.velocity = std::move<Vector3>(position / time_delta);
                    // temp_jd.position.rotate(Vector3(0, 1, 0), anim_data.feature_vector.y_rotation);
                    // temp_jd.velocity.rotate(Vector3(0, 1, 0), anim_data.feature_vector.y_rotation);
                    feature_vector.joint_data.set(i, temp_jd);
                }
            }
            anim_data.ignored = animation_length - key_time <= anim_blend_time + sample_step; // Ignore frames near the end
            anim_data.feature_vector = feature_vector_to_float_vector(feature_vector);
            /*test
			HashMap<String, int>::Iterator it = motion_matching::AnimData::bone_name_id_map.begin();
			HashMap<String, int>::Iterator end = motion_matching::AnimData::bone_name_id_map.end();

			for (it; it != end; ++it) {
				print_line(motion_matching::AnimData::bone_name_id_map.begin()->key);
				print_line(motion_matching::AnimData::bone_name_id_map.begin()->value);
			}*/
			//print_line (anim_data.pose_vector.anim_time, anim_data.feature_vector);
			animation_array.push_back(anim_data);
		}
    }
    standard_deviations = new float[motion_matching::AnimData::dimension];
    for(int i = 0; i != motion_matching::AnimData::dimension; ++i) {
        Vector<float> float_vec;
        for(int j = 0; j != animation_array.size(); ++j) {
            float_vec.push_back(animation_array[j].feature_vector[i]);
        }
        standard_deviations[i] = MMMath::standard_deviation(float_vec);
        float_vec.clear();
    }
    for(int i = 0; i != motion_matching::AnimData::dimension; ++i) {
        for(int j = 0; j != animation_array.size(); ++j) {
            animation_array.ptrw()[j].feature_vector.ptrw()[i] *= standard_deviations[i];
        }
    }
    int from = 0;
    while(from < animation_array.size()) {
        aabb_array.push_back(generate_aabb(from));
        from = aabb_array[aabb_array.size() - 1].upper_index + 1;
    }
    // Vector<float*> fv;
    // Vector<int> info;
    // for(int i = 0; i != animation_array.size(); ++i) {
    //     if(animation_array[i].ignored) continue;
    //     fv.push_back(feature_vector_to_float_array(animation_array[i].feature_vector));
    //     info.push_back(i);
    // }
    // kd_tree.initialize(fv, info, motion_matching::AnimData::dimension);

    parent = Object::cast_to<Node3D>(get_parent());
    animation_index = 0;
    print_line(animation_array.size());
	/*for (int i = 0; i < animation_array.size(); i++) {
		print_line(animation_array[i].feature_vector);
	}*/

	print_line("Process complete.");
}


void MotionMatching::anim_update(float delta) {
	delta *= speed_scale;
    Vector<motion_matching::JointData> pose_array;
    if(activated) {
		print_line("activated");
		if(!stoping && pending && !should_blend) {
			// stoping...
            stoping = true;
            cur_anim_time = pending_anim_pos;
            should_blend = true;
        }
        if(stoping) {
            cur_anim_time += delta;
			print_line("stoping");
			pose_array = calculate_pose_array(pending_anim, cur_anim_time);
        }

        else {
			print_line("activated else");
			float prev_frame_time = cur_anim_time;
            cur_anim_time += delta;
            int best_index = -1;
            //print_line("cur_anim_time ", cur_anim_time);
            while(cur_anim_time > sample_step) {
                //print_line("cur_anim_time ", cur_anim_time, " > ", "sample_step", sample_step);
                ++ match_cnt;
				//print_line("match_cnt ", match_cnt);
				if(match_cnt == 5) {
					//print_line("prev_frame_time");
					//print_line(prev_frame_time);
					auto cfv = calculate_feature_vector(prev_frame_time);
                    //print_line("cfv", cfv);
                    match_cnt = 0;

					best_index = find_best_match_index(cfv, delta);
					print_line("best_index: ", best_index, "animation_index: ", animation_index);
				}
				//print_line("best_index: ", best_index, "animation_index: ", animation_index);

				if(best_index != -1) {
                    should_blend = true;
					blend_pose_offset(animation_index, prev_frame_time, best_index);
					animation_index = best_index;
                    cur_anim_time = 0.f;
                }
                else {
                    cur_anim_time -= sample_step;
                    ++animation_index;
                }
			}
            cur_anim_name = animation_array[best_index].pose_vector.anim_name;
            //cur_anim_time = animation_array[best_index].pose_vector.anim_time;
			print_line("animation_index :", animation_index);//, "cur_anim_time :", cur_anim_time);
			pose_array = calculate_pose_array(animation_index, cur_anim_time);
		}
    }
    else if(pending) {
        print_line("pending");
        // starting...
		pending = false;
        activated = true;
        stoping = false;
        Vector<float> cfv;
        for(int i = 0; i != motion_matching::AnimData::dimension; ++i) {
            cfv.push_back(0.f);
        }
        int best_index = find_best_match_index(cfv, delta);
        should_blend = true;
		print_line("best_index: ", best_index);
		blend_pose_offset(
				pending_anim,
				pending_anim_pos,
				animation_player->get_animation(animation_array[best_index].pose_vector.anim_name),
				animation_array[best_index].pose_vector.anim_time);

		animation_index = best_index;
        cur_anim_time = 0.f;
        cur_anim_name = "";
        match_cnt = 0;
        cur_anim_name = animation_array[animation_index].pose_vector.anim_name;
        pose_array = calculate_pose_array(animation_index, cur_anim_time);
    }
    else {
        return;
    }

	if(should_blend) {
		print_line("should_blend");
		blend_time += delta;
		//blend_animation(pose_array, delta);
		if (blend_time > anim_blend_time) {
		    //print_line("blend_time ", blend_time, " > ", "anim_blend_time", anim_blend_time, "reset blend_time");
			blend_time = 0.f;
			should_blend = false;
			print_line("Blend finished");
			if(pending) {
				activated = false;
				pending = false;
				emit_signal("blend_complete");
			}
		}
	}
	//print_line("blend_time ", blend_time);

	set_skeleton_pose(pose_array);
	parent->set_rotation(Vector3(0.f, 0.f, 0.f));
	parent->rotate_y(MMMath::get_y_rotation(pose_array[motion_matching::AnimData::root_bone_index].quat));
}

void MotionMatching::generate_skeleton_map() const {
    motion_matching::AnimData::bone_name_id_map = {};
    motion_matching::AnimData::bone_cnt = skeleton->get_bone_count();
    for(int i = 0; i != motion_matching::AnimData::bone_cnt; ++i) {
        auto bone_name = skeleton->get_bone_name(i);
        motion_matching::AnimData::bone_name_id_map.insert(bone_name, i);
        if(skeleton->get_bone_parent(i) == -1)
            motion_matching::AnimData::root_bone_index = i;
        else if(skeleton->get_bone_parent(skeleton->get_bone_parent(i)) == -1)
            motion_matching::AnimData::hip_bone_index = i;
    }
}

void MotionMatching::initialize_bones_to_track(){
    Array temp;
    for(int i = 0; i < bones_to_track.size(); ++i) {
        String bone_name = bones_to_track[i];
        auto elem = motion_matching::AnimData::bone_name_id_map.find(bone_name);
        if(!elem) {
            print_line("Wrong bone name.");
            continue;
        }
        temp.push_back(bones_to_track[i]);
    }
    bones_to_track = temp;
}

void MotionMatching::set_joint_pose_track(Ref<Animation> animation, float time, int32_t track_index) {
    String bone_name = animation->track_get_path(track_index).get_subname(0);
    auto elem = motion_matching::AnimData::bone_name_id_map.find(bone_name);
    _ASSERT(elem);
    int bone_id = elem->value;
	if (animation->track_get_type(track_index) == Animation::TYPE_POSITION_3D) {
	    ;
		Vector3 pose_loc = animation->position_track_interpolate(track_index, time);
		skeleton->set_bone_pose_position(bone_id, pose_loc);
	} else if (animation->track_get_type(track_index) == Animation::TYPE_ROTATION_3D) {
	    Quaternion pose_quat = animation->rotation_track_interpolate(track_index, time);
		skeleton->set_bone_pose_rotation(bone_id, pose_quat);
	} else if (animation->track_get_type(track_index) == Animation::TYPE_SCALE_3D) {
		Vector3 pose_scale = animation->scale_track_interpolate(track_index, time);
		skeleton->set_bone_pose_scale(bone_id, pose_scale);
	}

		//transform.basis = Basis(pose_quat).scaled(pose_scale);
		//transform.origin = pose_loc;
}

void MotionMatching::set_skeleton_pose(const Vector<motion_matching::JointData>& pose_array) {
    int size = pose_array.size();
    //print_line("pose size");
    //print_line(size);
	for(int i = 0; i != size; ++i) {
        auto quat =pose_array[i].quat;
        //Transform transform = Transform(Basis(quat), pose_array[i].position);
        if(i == motion_matching::AnimData::root_bone_index) continue;
		//print_line("bone_index", i);
		//print_line("pos", pose_array[i].position);
		skeleton->set_bone_pose_position(i, pose_array[i].position);
		skeleton->set_bone_pose_rotation(i, quat);
            // transform.origin = Vector3(0, 0, 0);
	}
}

Vector<float> MotionMatching::calculate_feature_vector(float delta) const {
    Vector<float> result;

    auto ratio = delta / sample_step;
    _ASSERT(ratio <= 1.f && ratio >= 0.f);

	//print_line("motion_matching::AnimData::dimension");
	//print_line(motion_matching::AnimData::dimension);
	for(int i = 0; i != motion_matching::AnimData::dimension - 1; ++i) {
	//for(int i = 0; i != motion_matching::AnimData::dimension - 1; ++i) {
        /*print_line("animation_index");
        print_line(animation_index);
        print_line("i");
        print_line(i);
		print_line(animation_array[animation_index].feature_vector[i]);
		print_line(animation_array[animation_index + 1].feature_vector[i]);
		print_line("ratio");
		print_line(ratio);
		print_line(Math::lerp(animation_array[animation_index].feature_vector[i], animation_array[animation_index + 1].feature_vector[i], ratio));*/
        result.push_back(Math::lerp(animation_array[animation_index].feature_vector[i], animation_array[animation_index + 1].feature_vector[i], ratio));
		//result.push_back(Math::lerp(animation_array[animation_index].feature_vector[i], animation_array[animation_index].feature_vector[i + 1], ratio));
	}
	/*for (int i = 0; i != result.size(); ++i) {
        print_line(i);
        print_line(result[i]);
	}*/
	return result;
}

int MotionMatching::find_best_match_index(Vector<float> &cfv, float delta) {
	// Set trajectory
	//print_line("ENTER find_best_match_index");
	for(int i = 0; i != motion_matching::FeatureVector::trajectory_point_cnt; ++i) {
        int index = 6 * i + 3;
        cfv.ptrw()[index] = goal.trajectory[i].offset.x;
        cfv.ptrw()[index + 1] = goal.trajectory[i].offset.y;
        cfv.ptrw()[index + 2] = goal.trajectory[i].offset.z;
        cfv.ptrw()[index + 3] = goal.trajectory[i].direction.x;
        cfv.ptrw()[index + 4] = goal.trajectory[i].direction.y;
        cfv.ptrw()[index + 5] = goal.trajectory[i].direction.z;
    }

	// Set desired joint location
    for(int i = 0; i != bones_to_track.size(); ++i) {
        int index = 6 * (motion_matching::FeatureVector::trajectory_point_cnt + i) + 3;
        cfv.ptrw()[index] += cfv[index + 3] * delta;
        cfv.ptrw()[index + 1] += cfv[index + 4] * delta;
        cfv.ptrw()[index + 2] += cfv[index + 5] * delta;
    }

	int p = search(cfv);
    
	print_line("COMPARE");
	print_line("goal");
    Array printable_goal ;
    Array printable_cfv ;
	//auto float_goal = feature_vector_to_float_array(goal);
    for (int i = 0; i < motion_matching::FeatureVector::trajectory_point_cnt; i++) {
        int index = 6 * i + 3;
		printable_goal.push_back(goal.trajectory[i].offset);
		printable_goal.push_back(goal.trajectory[i].direction);
		printable_cfv.push_back(Vector3(cfv[index], cfv[index+1], cfv[index+2]));
		printable_cfv.push_back(Vector3(cfv[index], cfv[index+1], cfv[index+2]));
	}

	print_line(sizeof(printable_goal) / sizeof(printable_goal[0]));
	print_line("printable_goal");
	print_line(printable_goal);
	print_line("printable_cfv");
	print_line(printable_cfv);
	print_line(sizeof(cfv) / sizeof(cfv[0]));
	print_line("cfv");
	print_line(cfv);
	print_line("p.fv");
	print_line(animation_array[p].feature_vector);
	//print_line("fabs(animation_array[", p, "].pose_vector.anim_time - animation_array[", animation_index, "].pose_vector.anim_time", "=", animation_array[p].pose_vector.anim_time, " - ", animation_array[animation_index].pose_vector.anim_time, "=", fabs(animation_array[p].pose_vector.anim_time - animation_array[animation_index].pose_vector.anim_time), "<", blend_time);
    bool the_winner_is_at_the_same_location = animation_array[p].pose_vector.anim_name == cur_anim_name && fabs(animation_array[p].pose_vector.anim_time - animation_array[animation_index].pose_vector.anim_time) < blend_time;
	print_line("EXIT find_best_match_index");
	return the_winner_is_at_the_same_location ? -1 : p;
}

double cosine_similarity(Vector<float> &v1, Vector<float> &v2) {
	double dot_product = 0.0;
	double norm1 = 0.0;
	double norm2 = 0.0;
	for (int i = 0; i < v1.size(); i++) {
		dot_product += v1[i] * v2[i];
		norm1 += v1[i] * v1[i];
		norm2 += v2[i] * v2[i];
	}
	return dot_product / (sqrt(norm1) * sqrt(norm2));
}
/*
int MotionMatching::find_best_match_index(Vector<float> &input, float delta) {
	int most_similar_index = 0;
	double max_similarity = -INFINITY;

	for (int i = 0; i < animation_array.size(); i++) {
		Vector<float> fv = animation_array[i].feature_vector;
		double similarity = cosine_similarity(input, fv);
		if (similarity > max_similarity) {
			max_similarity = similarity;
			most_similar_index = i;
		}
	}
	return most_similar_index;
}
*/
void MotionMatching::set_goal(const Variant& trajectory) {
    Array arr = trajectory;
    if(arr.size() != 7) {
        for(int i = 0; i < motion_matching::FeatureVector::trajectory_point_cnt; ++i) {
            goal.trajectory[i].offset = goal.trajectory[i].direction = Vector3(0, 0, 0); // Reset
        }
        goal.root_velocity = Vector3(0, 0, 0);
        return;
    }
    for(int i = 0; i < motion_matching::FeatureVector::trajectory_point_cnt; ++i) {
        goal.trajectory[i].offset = arr[2 * i];
        Vector3 direction = arr[2 * i + 1];
        goal.trajectory[i].direction = direction.normalized();
    }
    goal.root_velocity = arr[6];
    goal.root_velocity.x *= standard_deviations[0];
    goal.root_velocity.y *= standard_deviations[1];
    goal.root_velocity.z *= standard_deviations[2];
    for(int i = 0; i != motion_matching::FeatureVector::trajectory_point_cnt; ++i) {
        int index = 3 + i * 6;
        goal.trajectory[i].offset.x *= standard_deviations[index];
        goal.trajectory[i].offset.y *= standard_deviations[index + 1];
        goal.trajectory[i].offset.z *= standard_deviations[index + 2];
        goal.trajectory[i].direction.x *= standard_deviations[index + 3];
        goal.trajectory[i].direction.y *= standard_deviations[index + 4];
        goal.trajectory[i].direction.z *= standard_deviations[index + 5];
    }
    print_line(goal.trajectory);
    print_line(goal.root_velocity);
    print_line(feature_vector_to_float_array(goal));
    // kd_tree.distance_func_class.goal = goal;
}

Vector<motion_matching::JointData> MotionMatching::calculate_pose_array(int index, float time_offset) const {
	return calculate_pose_array(animation_player->get_animation(animation_array[index].pose_vector.anim_name), time_offset + animation_array[index].pose_vector.anim_time);
}

Vector<motion_matching::JointData> MotionMatching::calculate_pose_array(Ref<Animation> animation, float position) const {
	Vector<motion_matching::JointData> result;
	result.resize(motion_matching::AnimData::bone_cnt);
    //print_line("position");
    //print_line(position);
    int track_cnt = animation->get_track_count();
    Vector3 jdposition;
	Vector3 jdvelocity;
	for(int track_index = 0; track_index < track_cnt; ++track_index) {
        auto bone_name = animation->track_get_path(track_index).get_subname(0);

        auto bone_id = motion_matching::AnimData::bone_name_id_map.find(bone_name)->value;

		motion_matching::JointData jd;
         
		if (animation->track_get_type(track_index) == Animation::TYPE_POSITION_3D) {
            jdposition = animation->position_track_interpolate(track_index, position);
			Vector3 delta_loc = animation->position_track_interpolate(track_index, position - time_delta);
			jdvelocity = std::move((jdposition - delta_loc) / time_delta);
		} else if (animation->track_get_type(track_index) == Animation::TYPE_ROTATION_3D) {
			jd.quat = animation->rotation_track_interpolate(track_index, position);
			jd.delta_quat = animation->rotation_track_interpolate(track_index, position - time_delta);
			jd.quat = MMMath::minial_angle_quat(jd.quat);
			jd.delta_quat = MMMath::minial_angle_quat(jd.delta_quat);
			jd.angular_velocity = MMMath::quat_to_angular_velocity(jd.quat * jd.delta_quat);
		} else if (animation->track_get_type(track_index) == Animation::TYPE_SCALE_3D) {
			Vector3 cur_scale = animation->scale_track_interpolate(track_index, position);
			Vector3 delta_scale = animation->scale_track_interpolate(track_index, position - time_delta);
        }
        jd.position = jdposition;
		jd.velocity = jdvelocity;
		result.set(bone_id, jd);
	}
    
	return result;
}

void MotionMatching::blend_animation(Vector<motion_matching::JointData>& pose_array, float delta) {
    float eydt = MMMath::fast_negexp(delta * MMMath::halflife_to_damping(blend_halflife) / 2.f);
	for(int i = 0; i != offset_pose_array.size(); ++i) {
        // auto inert = inert_paras[i];
        auto offset = offset_pose_array[i];
        auto jd = pose_array[i];
        if(fabs(1 - fabs(offset.quat.w)) > MMMath::eps) {
            MMMath::inertialize_update_quat(jd.quat, jd.angular_velocity, offset.quat, offset.angular_velocity, pose_array[i].quat, pose_array[i].angular_velocity, blend_halflife, delta, eydt);
        }
        MMMath::inertialize_update<Vector3>(jd.position, jd.velocity, offset.position, offset.velocity, pose_array[i].position, pose_array[i].velocity, blend_halflife, delta, eydt);
        offset_pose_array.set(i, offset);
        pose_array.set(i, jd);
    }
}

void MotionMatching::blend_pose_offset(int src_index, float src_time_offset, int dst_index) {
    blend_pose_offset(
        animation_player->get_animation(animation_array[src_index].pose_vector.anim_name),
        animation_array[src_index].pose_vector.anim_time + src_time_offset,
        animation_player->get_animation(animation_array[dst_index].pose_vector.anim_name),
        animation_array[dst_index].pose_vector.anim_time
    );
}

void MotionMatching::blend_pose_offset(Ref<Animation> src_anim, float src_anim_position, Ref<Animation> dest_anim, float dest_anim_position) {
	//print_line("src", src_anim_position);
	auto src = calculate_pose_array(src_anim, src_anim_position); // Current pose array
	//print_line("dst", dest_anim_position);
	auto dst = calculate_pose_array(dest_anim, dest_anim_position);
    for(int i = 0; i != motion_matching::AnimData::bone_cnt; ++i) {
        auto offset = offset_pose_array[i];
        // off.quat represents rotation from dst.quat to src.quat
        // the final pose shall be off.quat * dst.quat
        // during blending, as off_quat gradually decay to identity quaternion
        // final pose will approach dst_quat
        offset.quat = MMMath::minial_angle_quat(src[i].quat * dst[i].quat.inverse());
        offset.position = src[i].position - dst[i].position;
        offset.velocity = src[i].velocity - dst[i].velocity;
        offset.angular_velocity = src[i].angular_velocity - dst[i].angular_velocity;
        offset_pose_array.set(i, offset);
    }
}

float* MotionMatching::feature_vector_to_float_array(const motion_matching::FeatureVector& fv) {
    /**
     * 0, 2: root_velocity.xyz
     * 3: y_rotation
     * 4: y_rotational_velocity
     * next trajectory_cnt*6: trajectory
     * next joint_data.size()*6: joint_data
     */
    int fv_index = 0;
    float* float_arr = new float[motion_matching::AnimData::dimension];
    auto temp_arr = vector3_to_float_array(fv.root_velocity);
    for(int i = 0; i != 3; ++i) {
        float_arr[fv_index++] = temp_arr[i];
    }
    delete [] temp_arr;
    for(int i = 0; i != motion_matching::FeatureVector::trajectory_point_cnt; ++i) {
        temp_arr = vector3_to_float_array(fv.trajectory[i].offset);
        for(int j = 0; j != 3; ++j) {
            float_arr[fv_index++] = temp_arr[j];
        }
        delete [] temp_arr;
        temp_arr = vector3_to_float_array(fv.trajectory[i].direction);
        for(int j = 0; j != 3; ++j) {
            float_arr[fv_index++] = temp_arr[j];
        }
        delete [] temp_arr;
    }
    for(int i = 0; i != fv.joint_data.size(); ++i) {
        temp_arr = vector3_to_float_array(fv.joint_data[i].position);
        for(int j = 0; j != 3; ++j) {
            float_arr[fv_index++] = temp_arr[j];
        }
        delete [] temp_arr;
        temp_arr = vector3_to_float_array(fv.joint_data[i].velocity);
        for(int j = 0; j != 3; ++j) {
            float_arr[fv_index++] = temp_arr[j];
        }
        delete [] temp_arr;
    }
    float_arr[fv_index++] = fv.y_rotational_velocity;
    return float_arr;
}

float* MotionMatching::vector3_to_float_array(const Vector3& vec3) {
    float* arr = new float[3];
    arr[0] = vec3.x;
    arr[1] = vec3.y;
    arr[2] = vec3.z;
    return arr;
}

Vector<float> MotionMatching::feature_vector_to_float_vector(const motion_matching::FeatureVector& fv) {
    auto float_arr = feature_vector_to_float_array(fv);
    Vector<float> vec;
    for(int i = 0; i != motion_matching::AnimData::dimension; ++i) {
        vec.push_back(float_arr[i]);
    }
    delete [] float_arr;
    return vec;
}

MMAABB MotionMatching::generate_aabb(int from, int to) {
    MMAABB aabb(animation_array[from].feature_vector, animation_array[from].feature_vector, from, from);
    ++from;
    while(from <= to) {
        aabb = MMAABB::aabb_union(aabb, MMAABB(animation_array[from].feature_vector, animation_array[from].feature_vector, from, from));
        ++from;
    }
    return aabb;
}

MMAABB MotionMatching::generate_aabb(int from) {
    MMAABB aabb(animation_array[from].feature_vector, animation_array[from].feature_vector, from, from);
    auto begin_name = animation_array[from].pose_vector.anim_name;
    int to = from;
    // Find range
    while(to < animation_array.size() && animation_array[to].pose_vector.anim_name == begin_name && !animation_array[to].ignored && to - from < 64) {
        ++to;
    }
    --to;
    while(from <= to) {
        aabb.children.push_back(generate_aabb(from, MIN(from + 15, to)));
        from += 16;
    }
    MMAABB temp = aabb;
    for(int i = 0; i != aabb.children.size(); ++i) {
        temp = MMAABB::aabb_union(temp, aabb.children[i]);
    }
    aabb.lower_bound = temp.lower_bound;
    aabb.upper_bound = temp.upper_bound;
    aabb.lower_index = temp.lower_index;
    aabb.upper_index = temp.upper_index;
    return aabb;
}

int MotionMatching::search(const Vector<float>& fv) {
    float max_dist = 1e20f;
    float best_index = -1;
    for(int i = 0; i != aabb_array.size(); ++i) {
        // Search outside
        float outer_dist = 0.f;
        for(int j = 0; j != fv.size(); j++) {
            float nearest = CLAMP(fv[j], aabb_array[i].lower_bound[j], aabb_array[i].upper_bound[j]);
            float temp = fv[j] - nearest;
            outer_dist += temp * temp;
            if(outer_dist > max_dist) goto end_of_outside;
        }
        // Search children
        for(int j = 0; j != aabb_array[i].children.size(); ++j) {
            outer_dist = 0.f;
            for(int k = 0; k != fv.size(); ++k) {
                float nearest = CLAMP(fv[k], aabb_array[i].children[j].lower_bound[k], aabb_array[i].children[j].upper_bound[k]);
                float temp = fv[k] - nearest;
                outer_dist += temp * temp;
                if(outer_dist > max_dist) goto end_of_children;
            }
            // Search inside
            for(int k = aabb_array[i].children[j].lower_index; k <= aabb_array[i].children[j].upper_index; ++k) {
                outer_dist = 0.f;
                for(int l = 0; l != fv.size(); ++l) {
                    float temp = fv[l] - animation_array[k].feature_vector[l];
                    outer_dist += temp * temp;
                    if(outer_dist > max_dist) goto end_of_inside;
                }
                max_dist = outer_dist;
                best_index = k;
                end_of_inside:;
            }
            end_of_children:;
        }
        end_of_outside:;
    }
    return best_index;
}

void MotionMatching::flip_flop(Ref<Animation> animation, float anim_pos) {
	if(pending) return;
    pending = true;
    pending_anim = animation;
    pending_anim_pos = anim_pos;
}