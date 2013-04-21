
#include <stdio.h>
#include <stdarg.h>
#include <fstream>

#include "urdf.h"

#define BOT_VIS_SUPPORT  // adds a bunch of dependencies, which are not necessary for all functionality

#ifdef BOT_VIS_SUPPORT
#include <bot_core/bot_core.h>
#include <bot_vis/bot_vis.h>
#endif

#include <boost/shared_ptr.hpp>
#include <boost/filesystem.hpp>

using namespace std;

// Defines a method to load a model directly from a URDF file

void ROS_ERROR(const char* format, ...) {
  va_list vl;
  va_start(vl, format);
  vprintf(format, vl);
  va_end(vl);  
  printf("\n");
  exit(1);
}

void mexErrMsgIdandTxt(const char *errorid, const char *errormsg, ...)
{
  va_list vl;
  va_start(vl, errormsg);
  vprintf(errormsg, vl);
  va_end(vl);  
  printf("\n");
  exit(1);
}

URDFRigidBodyManipulator::URDFRigidBodyManipulator(boost::shared_ptr<urdf::ModelInterface> _urdf_model, std::map<std::string, int> jointname_to_jointnum, const std::string & root_dir) 
: 
  RigidBodyManipulator((int)jointname_to_jointnum.size()),
  joint_map(jointname_to_jointnum),
          urdf_model(_urdf_model)
{
    // set up floating base
    // note: i see no harm in adding the floating base here (even if the drake version does not have one)
    // because the base will be set to 0 and it adds minimal expense to the kinematic calculations
  {
    this->bodies[0].linkname = "world";
    this->parent[0] = -1;
    this->pitch[0] = INF;
    this->bodies[1].linkname = this->bodies[1].jointname = "base_x";
    this->bodies[1].T_body_to_joint << 0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1;
    
    this->parent[1] = 0;
    this->pitch[1] = INF;
    this->bodies[2].linkname = this->bodies[2].jointname = "base_y";
    this->bodies[2].T_body_to_joint << 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1;
    
    this->parent[2] = 1;
    this->pitch[2] = INF;
    this->bodies[3].linkname = this->bodies[3].jointname = "base_z";
    
    this->parent[3] = 2;
    this->pitch[3] = 0;
    this->bodies[4].linkname = this->bodies[4].jointname = "base_roll";
    this->bodies[4].T_body_to_joint << 0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1;
    
    this->parent[4] = 3;
    this->pitch[4] = 0;
    this->bodies[5].linkname = this->bodies[5].jointname = "base_pitch";
    this->bodies[5].T_body_to_joint << 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1;
    
    this->parent[5] = 4;
    this->pitch[5] = 0;
    this->bodies[6].jointname = "base_yaw";
  }
  
  int index=0;
  for (std::map<std::string, boost::shared_ptr<urdf::Joint> >::iterator j=urdf_model->joints_.begin(); j!=urdf_model->joints_.end(); j++) {
    std::map<std::string, int>::iterator jn=joint_map.find(j->first);
    if (jn == joint_map.end()) ROS_ERROR("can't find joint %s.  this shouldn't happen", j->first.c_str());
    index = jn->second;
    
    // set parent
    this->bodies[index+1].linkname = j->second->child_link_name;
    if (!j->second->parent_link_name.empty()) {
      std::map<std::string, boost::shared_ptr<urdf::Link> >::iterator plink = urdf_model->links_.find(j->second->parent_link_name);
      if (plink != urdf_model->links_.end() && plink->second->parent_joint.get()) {
        // j has a parent, find its index
        std::map<std::string, int>::iterator j2 = joint_map.find(plink->second->parent_joint->name);
        if (j2 == joint_map.end()) ROS_ERROR("can't find index of parent %s of link %s.", plink->second->parent_joint->name.c_str(), plink->second->name.c_str());
        this->parent[index] = j2->second;
//          printf("%s parent %d\n",j->second->child_link_name.c_str(),j2->second);
      } else {
        this->parent[index] = 5;  // no parent: attach it to the floating base
        this->bodies[6].linkname = j->second->parent_link_name;
      }
    } else {
      this->parent[index] = 5;  // no parent: attach it to the floating base
      this->bodies[6].linkname = j->second->parent_link_name;
    }
    
    // set pitch
    switch (j->second->type) {
      case urdf::Joint::REVOLUTE:
      case urdf::Joint::FIXED:  // treat fixed joints as revolute (that are always at zero)
        this->pitch[index] = 0.0;
        break;
      case urdf::Joint::PRISMATIC:
        this->pitch[index] = INF;
        break;
    }
    
    // setup kinematic tree
    {
      double x=j->second->parent_to_joint_origin_transform.position.x,
              y=j->second->parent_to_joint_origin_transform.position.y,
              z=j->second->parent_to_joint_origin_transform.position.z;
      double qx,qy,qz,qw;
      j->second->parent_to_joint_origin_transform.rotation.getQuaternion(qx,qy,qz,qw);
      this->bodies[index+1].Ttree <<
              qw*qw + qx*qx - qy*qy - qz*qz, 2*qx*qy - 2*qw*qz, 2*qx*qz + 2*qw*qy, x,
              2*qx*qy + 2*qw*qz,  qw*qw + qy*qy - qx*qx - qz*qz, 2*qy*qz - 2*qw*qx, y,
              2*qx*qz - 2*qw*qy, 2*qy*qz + 2*qw*qx, qw*qw + qz*qz - qx*qx - qy*qy, z,
              0, 0, 0, 1;
      
      Vector3d zvec; zvec << 0,0,1;
      Vector3d joint_axis; joint_axis << j->second->axis.x, j->second->axis.y, j->second->axis.z;
      Vector3d axis = joint_axis.cross(zvec);
      double angle = acos(joint_axis.dot(zvec));
      if (axis.squaredNorm()<.0001) //  then it's a scaling of the z axis.
        axis << 0,1,0;
      axis.normalize();
      qw=cos(angle/2.0);
      qx=axis(0)*sin(angle/2.0);
      qy=axis(1)*sin(angle/2.0);
      qz=axis(2)*sin(angle/2.0);
      
      this->bodies[index+1].T_body_to_joint <<
              qw*qw + qx*qx - qy*qy - qz*qz, 2*qx*qy - 2*qw*qz, 2*qx*qz + 2*qw*qy, 0,
              2*qx*qy + 2*qw*qz,  qw*qw + qy*qy - qx*qx - qz*qz, 2*qy*qz - 2*qw*qx, 0,
              2*qx*qz - 2*qw*qy, 2*qy*qz + 2*qw*qx, qw*qw + qz*qz - qx*qx - qy*qy, 0,
              0, 0, 0, 1;
    }
  }

#ifdef BOT_VIS_SUPPORT
  // load mesh geometry
  for (std::map<std::string, boost::shared_ptr<urdf::Link> >::iterator l=urdf_model->links_.begin(); l!=urdf_model->links_.end(); l++) {
    if (l->second->visual) { // then at least one default visual tag exists
      map<string, boost::shared_ptr<vector<boost::shared_ptr<urdf::Visual> > > >::iterator v_grp_it = l->second->visual_groups.find("default");
      for (size_t iv = 0;iv < v_grp_it->second->size();iv++)
      {
        vector<boost::shared_ptr<urdf::Visual> > visuals = (*v_grp_it->second);
        if (visuals[iv]->geometry->type == urdf::Geometry::MESH) {
          boost::shared_ptr<urdf::Mesh> mesh(boost::dynamic_pointer_cast<urdf::Mesh>(visuals[iv]->geometry));
          
          bool has_package = boost::find_first(mesh->filename,"package://");
          std::string fname = root_dir + "/" + mesh->filename;
          if (has_package) boost::replace_first(fname,"package:/","..");
          boost::filesystem::path mypath(fname);
          
          if (!boost::filesystem::exists(fname)) {
            cerr << "cannot find mesh file: " << fname;
            if (has_package)
              cerr << " (note: original mesh string had a package:// in it)";
            cerr << endl;
            continue;
          }
          
          std::string ext = mypath.extension().native();
          boost::to_lower(ext);
          
          if (ext.compare(".dae")==0) {
            BotWavefrontModel* wavefront_model = bot_wavefront_model_create(fname.c_str());
            if (!wavefront_model) {
              cerr << "Error loading mesh: " << fname << endl;
            }
          } else
            cout << "Warning: Mesh " << fname << " ignored because it does not have extension .dae" << endl;
        }
      }
    }
  }  
#endif

  compile();  
}


namespace urdf {

// pulled directly from ROS (actually from drc/software/robot_model/urdf_parser/)
boost::shared_ptr<ModelInterface> parseURDF(const std::string &xml_string)
{
  boost::shared_ptr<ModelInterface> model(new ModelInterface);
  model->clear();

  TiXmlDocument xml_doc;
  xml_doc.Parse(xml_string.c_str());

  TiXmlElement *robot_xml = xml_doc.FirstChildElement("robot");
  if (!robot_xml)
  {
    ROS_ERROR("Could not find the 'robot' element in the xml file");
    model.reset();
    return model;
  }

  // Get robot name
  const char *name = robot_xml->Attribute("name");
  if (!name)
  {
    ROS_ERROR("No name given for the robot.");
    model.reset();
    return model;
  }
  model->name_ = std::string(name);

  // Get all Material elements
  for (TiXmlElement* material_xml = robot_xml->FirstChildElement("material"); material_xml; material_xml = material_xml->NextSiblingElement("material"))
  {
    boost::shared_ptr<Material> material;
    material.reset(new Material);

    try {
      material->initXml(material_xml);
      if (model->getMaterial(material->name))
      {
        ROS_ERROR("material '%s' is not unique.", material->name.c_str());
        material.reset();
        model.reset();
        return model;
      }
      else
      {
        model->materials_.insert(make_pair(material->name,material));
        //ROS_DEBUG("successfully added a new material '%s'", material->name.c_str());
      }
    }
    catch (ParseError &e) {
      ROS_ERROR("material xml is not initialized correctly");
      material.reset();
      model.reset();
      return model;
    }
  }

  // Get all Link elements
  for (TiXmlElement* link_xml = robot_xml->FirstChildElement("link"); link_xml; link_xml = link_xml->NextSiblingElement("link"))
  {
    boost::shared_ptr<Link> link;
    link.reset(new Link);

    try {
      link->initXml(link_xml);
      if (model->getLink(link->name))
      {
        ROS_ERROR("link '%s' is not unique.", link->name.c_str());
        model.reset();
        return model;
      }
      else
      {
        // set link visual material
        //ROS_DEBUG("setting link '%s' material", link->name.c_str());
        if (link->visual)
        {
          if (!link->visual->material_name.empty())
          {
            if (model->getMaterial(link->visual->material_name))
            {
              //ROS_DEBUG("setting link '%s' material to '%s'", link->name.c_str(),link->visual->material_name.c_str());
              link->visual->material = model->getMaterial( link->visual->material_name.c_str() );
            }
            else
            {
              if (link->visual->material)
              {
                //ROS_DEBUG("link '%s' material '%s' defined in Visual.", link->name.c_str(),link->visual->material_name.c_str());
                model->materials_.insert(make_pair(link->visual->material->name,link->visual->material));
              }
              else
              {
                ROS_ERROR("link '%s' material '%s' undefined.", link->name.c_str(),link->visual->material_name.c_str());
                model.reset();
                return model;
              }
            }
          }
        }

        model->links_.insert(make_pair(link->name,link));
        //ROS_DEBUG("successfully added a new link '%s'", link->name.c_str());
      }
    }
    catch (ParseError &e) {
      ROS_ERROR("link xml is not initialized correctly");
      model.reset();
      return model;
    }
  }
  if (model->links_.empty()){
    ROS_ERROR("No link elements found in urdf file");
    model.reset();
    return model;
  }

  // Get all Joint elements
  for (TiXmlElement* joint_xml = robot_xml->FirstChildElement("joint"); joint_xml; joint_xml = joint_xml->NextSiblingElement("joint"))
  {
    boost::shared_ptr<Joint> joint;
    joint.reset(new Joint);

    if (joint->initXml(joint_xml))
    {
      if (model->getJoint(joint->name))
      {
        ROS_ERROR("joint '%s' is not unique.", joint->name.c_str());
        model.reset();
        return model;
      }
      else
      {
        model->joints_.insert(make_pair(joint->name,joint));
        //ROS_DEBUG("successfully added a new joint '%s'", joint->name.c_str());
      }
    }
    else
    {
      ROS_ERROR("joint xml is not initialized correctly");
      model.reset();
      return model;
    }
  }


  // every link has children links and joints, but no parents, so we create a
  // local convenience data structure for keeping child->parent relations
  std::map<std::string, std::string> parent_link_tree;
  parent_link_tree.clear();

  // building tree: name mapping
  if (!model->initTree(parent_link_tree))
  {
    ROS_ERROR("failed to build tree");
    model.reset();
    return model;
  }

  // find the root link
  if (!model->initRoot(parent_link_tree))
  {
    ROS_ERROR("failed to find root link");
    model.reset();
    return model;
  }
 
  return model;
}

}

void setJointNum(boost::shared_ptr<urdf::ModelInterface> urdf_model, boost::shared_ptr<urdf::Joint> j, std::map<std::string, int> & jointname_to_jointnum)
{
  std::map<std::string, int>::iterator ans = jointname_to_jointnum.find(j->name);
  if (ans != jointname_to_jointnum.end()) // then i've already got a joint num
    return;

//  printf("setting joint num for %s\n", j->name.c_str());
  
  // recursively set parent num, then set j
  if (!j->parent_link_name.empty()) {
    std::map<std::string, boost::shared_ptr<urdf::Link> >::iterator plink = urdf_model->links_.find(j->parent_link_name);
    if (plink != urdf_model->links_.end()) {
      if (plink->second->parent_joint.get()) {
        // j has a parent
//        printf("  ");
        setJointNum(urdf_model,plink->second->parent_joint,jointname_to_jointnum);
      }
    }
  }
  
  switch (j->type) {
    case urdf::Joint::REVOLUTE:
    case urdf::Joint::CONTINUOUS:
    case urdf::Joint::PRISMATIC:
    case urdf::Joint::FIXED:
      jointname_to_jointnum.insert(std::make_pair(j->name,(int)jointname_to_jointnum.size()));
      break;
    case urdf::Joint::FLOATING:
      ROS_ERROR("(internal) FLOATING joints not supported yet.");
      // the problem is that if the joint name here is not the same as 
      // j->name, then future searches will fail.  it's not hard to 
      // accomodate, but i'll skip it on the first pass

      jointname_to_jointnum.insert(std::make_pair(j->name+"_x",(int)jointname_to_jointnum.size()));
      jointname_to_jointnum.insert(std::make_pair(j->name+"_y",(int)jointname_to_jointnum.size()));
      jointname_to_jointnum.insert(std::make_pair(j->name+"_z",(int)jointname_to_jointnum.size()));
      jointname_to_jointnum.insert(std::make_pair(j->name+"_yaw",(int)jointname_to_jointnum.size()));
      jointname_to_jointnum.insert(std::make_pair(j->name+"_pitch",(int)jointname_to_jointnum.size()));
      jointname_to_jointnum.insert(std::make_pair(j->name+"_roll",(int)jointname_to_jointnum.size()));

      // idea: could also insert the original joint name
      jointname_to_jointnum.insert(std::make_pair(j->name,-1));
      break;
    case urdf::Joint::PLANAR:
      ROS_ERROR("PLANAR joints not supported yet.");
      break;
    default:
      ROS_ERROR("unsupported joint type %d for joint %s.", j->type, j->name.c_str());
      break;
      break;
  }
}

URDFRigidBodyManipulator* loadURDFfromXML(const std::string &xml_string, const std::string &root_dir)
{
  // call ROS urdf parsing
  boost::shared_ptr<urdf::ModelInterface> urdf_model = urdf::parseURDF(xml_string);

  // produce a joint number for each joint where the parent has a
  // lower number than all of it's children.
  // NOTE:  The joint number does not necessarily match the number in
  // matlab.  I'm not removing fixed joints here.  I'm always adding the
  // the floating base joints.  But if these transformations are unused,
  // then they will simply stay at zero.
  std::map<std::string, int> jointname_to_jointnum;
  {  
    int jointnum=0;
    jointname_to_jointnum.insert(std::make_pair("base_x",jointnum++));
    jointname_to_jointnum.insert(std::make_pair("base_y",jointnum++));
    jointname_to_jointnum.insert(std::make_pair("base_z",jointnum++));
    jointname_to_jointnum.insert(std::make_pair("base_yaw",jointnum++));
    jointname_to_jointnum.insert(std::make_pair("base_pitch",jointnum++));
    jointname_to_jointnum.insert(std::make_pair("base_roll",jointnum++));
    
    for (std::map<std::string, boost::shared_ptr<urdf::Joint> >::iterator j=urdf_model->joints_.begin(); j!=urdf_model->joints_.end(); j++)
      setJointNum(urdf_model,j->second,jointname_to_jointnum);
    
//    for (std::map<std::string, int>::iterator j=jointname_to_jointnum.begin(); j!=jointname_to_jointnum.end(); j++)
//      printf("%s : %d\n",j->first.c_str(),j->second);
  }
  
  // now populate my model class
  URDFRigidBodyManipulator* model = new URDFRigidBodyManipulator(urdf_model,jointname_to_jointnum,root_dir);
  

  return model;
}

URDFRigidBodyManipulator* loadURDFfromFile(const std::string &urdf_filename)
{
  std::string xml_string;
  std::fstream xml_file(urdf_filename.c_str(), std::fstream::in);
  if (xml_file.is_open())
  {
    while ( xml_file.good() )
    {
      std::string line;
      std::getline( xml_file, line);
      xml_string += (line + "\n");
    }
    xml_file.close();
  }
  else
  {
    std::cerr << "Could not open file ["<<urdf_filename.c_str()<<"] for parsing."<< std::endl;
    return NULL;
  }
  
  boost::filesystem::path mypath(urdf_filename);
  std::string pathname(mypath.parent_path().native());
  
  // parse URDF to get model
  return loadURDFfromXML(xml_string,pathname);
}