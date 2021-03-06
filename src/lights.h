#ifndef LIGHTS_H
#define LIGHTS_H

#include "fast_math.h"
#include "assimp/light.h"

//class representing a general light object
class Light {
	public:
		//method to figure out a light's direction and color based on a point given
		virtual void get_dir_c(vec3 r, vec3 c, const vec3 ppos);
		//method to only get direction
		virtual void get_dir(vec3 r, const vec3 ppos);
};

class DirectLight : public Light {
	private:
		vec3 color;
		vec3 direction;
	public:
		//construct a directlight from an aiLight type
		DirectLight(aiLight* light);
		//method to get direction and color
		void get_dir_c(vec3 r, vec3 c, const vec3 ppos);
		//method to only get direction
		void get_dir(vec3 r, const vec3 ppos);
};

class PointLight : public Light {
	private:
		vec3 color;
		vec3 pos;
	public:
		PointLight(aiLight* light);
		//method to get direction and color
		void get_dir_c(vec3 r, vec3 c, const vec3 ppos);
		//method to only get direction
		void get_dir(vec3 r, const vec3 ppos);
};

#endif
