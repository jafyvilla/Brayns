/* Copyright (c) 2015-2016, EPFL/Blue Brain Project
 * All rights reserved. Do not distribute without permission.
 * Responsible Author: Cyrille Favreau <cyrille.favreau@epfl.ch>
 *
 * This file is part of Brayns <https://github.com/BlueBrain/Brayns>
 *
 * Based on OSPRay implementation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3.0 as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <plugins/engines/ospray/render/utils/AbstractRenderer.ih>

struct ProximityRenderer
{
    AbstractRenderer abstract;

    vec3f nearColor;
    vec3f farColor;
    float detectionDistance;
    bool detectionOnDifferentMaterial;
    bool electronShadingEnabled;
};

inline vec3f ProximityRenderer_shadeRay(
        const uniform ProximityRenderer* uniform self,
        varying ScreenSample& sample)
{
    Ray ray = sample.ray;

    varying vec3f color = make_vec3f( 0.f );
    varying float path_opacity = 1.f;
    varying vec3f colorKs = make_vec3f( 0.f );

    traceRay( self->abstract.super.model, ray );

    if( ray.geomID < 0 )
    {
        // No intersection. Return skybox color

        color = color +
            path_opacity * make_vec3f( skyboxMapping(
                (Renderer *)self, ray, self->abstract.numMaterials, self->abstract.materials ));

        sample.z = 1.f;
        sample.alpha = 0.f;
        return self->abstract.bgColor + color + colorKs;
    }

    sample.alpha = 1.f;
    sample.z = log( ray.t ) / 10.f;

    DifferentialGeometry dg;
    postIntersect(
                self->abstract.super.model,
                dg,
                ray,
                DG_NG|DG_NS|DG_NORMALIZE|DG_FACEFORWARD|DG_MATERIALID|
                DG_COLOR|DG_TEXCOORD);

    uniform Material *material = dg.material;
    uniform ExtendedOBJMaterial *objMaterial =
            (uniform ExtendedOBJMaterial *)material;

    varying float emission = 0.f;
    varying float opacity = 1.f;
    varying float Ns = 0.f;
    varying vec3f Kd = make_vec3f( 0.0f, 0.0f, 0.0f );
    varying vec3f Ks = make_vec3f( 0.f );
    varying vec3f normal = dg.Ns;
    varying const vec3f P = dg.P + self->abstract.super.epsilon * dg.Ng;

    if( !objMaterial )
    {
        opacity = dg.color.w;
        Kd = make_vec3f( dg.color );
    }
    else
    {
        foreach_unique( mat in objMaterial )
        {
            emission = mat->a;
            opacity = mat->d * get1f( mat->map_d, dg.st, 1.f );
            Kd = mat->Kd * make_vec3f( dg.color );
        }
    }

    const varying float local_opacity = path_opacity * abs( opacity );

    if( local_opacity > 0.01f )
    {
        const varying vec3f R =
            ray.dir - ( 2.f * dot( ray.dir, normal )) * normal;

        //calculate shading for all lights
        for( uniform int i = 0; self->abstract.lights && i < self->abstract.numLights; i++ )
        {
            const uniform Light* uniform l = self->abstract.lights[i];

            const varying vec2f s = make_vec2f( 0.f );
            const varying Light_SampleRes light = l->sample( l, dg, s );
            float varying radiance = 1.f;
            varying vec3f L = light.dir;
            varying float dist;

            if(emission != 0 )
                radiance = emission;
            else
                radiance = reduce_max( light.weight );

            if( radiance > 0.f )
            {
                // Any potential contribution?
                varying float cosNL = 0.0;
                varying float cosLR = 0.0;
                if( self->electronShadingEnabled )
                {
                    // Electron shading
                    vec3f me = normalize( ray.org - P );
                    cosNL = 0.2f - 0.2f * max( 0.f, dot( me, normal ));
                }
                else
                {
                    cosNL = max( 0, dot( L, normal ));
                    cosLR = max( 0, dot( L, R ));
                }

                const varying vec3f unshaded_light_contrib =
                        local_opacity * Kd * cosNL * radiance;
                // Specular
                colorKs = max( colorKs, Ks * powf( cosLR, Ns ));

                color = color + unshaded_light_contrib;
            }
        }

        if (self->detectionDistance>0.f && ray.t<self->detectionDistance*1000.f )
        {
            // Generate random ray and trace it
            varying vec3f ao_dir = getRandomVector( sample, normal, self->abstract.randomNumber );

            if( dot( ao_dir, normal ) < 0.f )
                ao_dir = ao_dir * -1.f;
            varying Ray ao_ray = ray;
            ao_ray.org = P;
            ao_ray.dir = ao_dir;
            varying float t_max = self->detectionDistance;
            ao_ray.t0 = self->detectionDistance * 0.1f;
            ao_ray.t = t_max;

            traceRay( self->abstract.super.model, ao_ray );
            if( ao_ray.t != t_max )
            {
                // Intersection detected
                postIntersect( self->abstract.super.model, dg, ao_ray, DG_MATERIALID );

                varying bool test = true;
                if( self->detectionOnDifferentMaterial )
                    test =  material != dg.material;

                if( test )
                {
                    if( ao_ray.t < t_max * 0.2f )
                        color = self->nearColor * opacity / ao_ray.t;
                    else
                        color = self->farColor * opacity / ao_ray.t;
                }
            }
        }
    }

    return color;
}

void ProximityRenderer_renderSample(
    uniform Renderer* uniform _self,
    void * uniform perFrameData,
    varying ScreenSample& sample)
{
    uniform ProximityRenderer* uniform self =
            ( uniform ProximityRenderer* uniform )_self;
    sample.ray.time = self->abstract.timestamp;
    sample.rgb = ProximityRenderer_shadeRay( self, sample );
}


// Exports (called from C++)
export void *uniform ProximityRenderer_create( void* uniform cppE )
{
    uniform ProximityRenderer* uniform self =
            uniform new uniform ProximityRenderer;

    Renderer_Constructor( &self->abstract.super, cppE );
    self->abstract.super.renderSample = ProximityRenderer_renderSample;
    return self;
}

export void ProximityRenderer_set(
        void *uniform _self,
        const uniform vec3f& bgColor,
        const uniform vec3f& nearColor,
        const uniform vec3f& farColor,
        const uniform float& detectionDistance,
        const uniform bool& detectionOnDifferentMaterial,
        const uniform int& randomNumber,
        const uniform float& timestamp,
        const uniform int& spp,
        const uniform bool& electronShadingEnabled,
        void** uniform lights,
        uniform int32 numLights,
        void** uniform materials,
        uniform int32 numMaterials )
{
    uniform ProximityRenderer *uniform self = ( uniform ProximityRenderer* uniform )_self;

    self->abstract.bgColor = bgColor;
    self->abstract.randomNumber = randomNumber;
    self->abstract.timestamp = timestamp;
    self->abstract.spp = spp;
    self->abstract.electronShadingEnabled = electronShadingEnabled;
    self->abstract.lights = ( const uniform Light* uniform* uniform )lights;
    self->abstract.numLights = numLights;
    self->abstract.materials = ( const uniform ExtendedOBJMaterial* uniform* uniform )materials;
    self->abstract.numMaterials = numMaterials;

    self->nearColor = nearColor;
    self->farColor = farColor;
    self->detectionDistance = detectionDistance;
    self->detectionOnDifferentMaterial = detectionOnDifferentMaterial;
}
