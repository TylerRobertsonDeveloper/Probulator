#include <stdio.h>
#include "Common.h"
#include "Image.h"
#include "SphericalGaussian.h"
#include "SphericalHarmonics.h"

using namespace Probulator;

int main(int argc, char** argv)
{
#if 1

	if (argc < 2)
	{
		printf("Usage: Probulator <LatLongEnvmap.hdr>\n");
		return 1;
	}

	const char* inputFilename = argv[1];
	Image inputImage;
	if (!inputImage.readHdr(inputFilename))
	{
		printf("ERROR: Failed to read input image from file '%s'\n", inputFilename);
		return 1;
	}

	auto getSample = [&](vec3 direction)
	{
		return (vec3)inputImage.sampleNearest(cartesianToLatLongTexcoord(direction));
	};

#else

	auto getSample = [&](vec3 direction)
	{
		//return vec3(max(0.0f, -direction.z));
		return 10.0f * vec3(pow(max(0.0f, -direction.z), 100.0f));
		//return 200.0f * vec3(pow(max(0.0f, -direction.z), 100.0f));
		//return vec3(1.0f);
	};

#endif

	const ivec2 outputImageSize(256, 128);

	//////////////////////
	// Generate SG basis
	//////////////////////

	const u32 lobeCount = 12; // <-- tweak this
	const float lambda = 0.5f * lobeCount; // <-- tweak this; 

	SphericalGaussian lobes[lobeCount];
	std::vector<vec3> sgLobeDirections;
	for (u32 lobeIt = 0; lobeIt < lobeCount; ++lobeIt)
	{
		lobes[lobeIt].p = sampleVogelsSphere(lobeIt, lobeCount);
		lobes[lobeIt].lambda = lambda;
		lobes[lobeIt].mu = vec3(0.0f);
	}

	const float sgNormFactor = fourPi / sgIntegral(lambda); // TODO: there is no solid basis for this right now
	
	////////////////////////////////////////////
	// Generate radiance image (not convolved)
	////////////////////////////////////////////

	float averageRadianceSample = 0.0f;
	Image radianceImage(outputImageSize);
	radianceImage.forPixels2D([&](vec4& pixel, ivec2 pixelPos)
	{
		vec2 uv = (vec2(pixelPos) + vec2(0.5f)) / vec2(outputImageSize - ivec2(1));
		vec3 direction = latLongTexcoordToCartesian(uv);
		vec3 sample = getSample(direction);

		pixel = vec4(sample, 1.0f);

		averageRadianceSample += sample.r;
	});

	radianceImage.writePng("radiance.png");

	averageRadianceSample /= radianceImage.getPixelCount();	
	printf("Average radiance: %f\n", averageRadianceSample);

	/////////////////////
	// Project radiance
	/////////////////////

	SphericalHarmonicsL2RGB shRadiance;

	const u32 sampleCount = 20000;
	for (u32 sampleIt = 0; sampleIt < sampleCount; ++sampleIt)
	{
		vec2 sampleUv = sampleHammersley(sampleIt, sampleCount);
		vec3 direction = sampleUniformSphere(sampleUv);

		vec3 sample = getSample(direction);

		for (u32 lobeIt = 0; lobeIt < lobeCount; ++lobeIt)
		{
			const SphericalGaussian& sg = lobes[lobeIt];
			float w = sgEvaluate(sg.p, sg.lambda, direction);
			lobes[lobeIt].mu += sample * sgNormFactor * (w / sampleCount);
		}

		shAddWeighted(shRadiance, shEvaluateL2(direction), sample * (fourPi / sampleCount));
	}

	///////////////////////////////////////////
	// Generate reconstructed radiance images
	///////////////////////////////////////////

	float averageRadianceSgSample = 0.0f;
	float averageRadianceShSample = 0.0f;
	Image radianceSgImage(outputImageSize);
	Image radianceShImage(outputImageSize);

	radianceSgImage.forPixels2D([&](vec4&, ivec2 pixelPos)
	{
		vec2 uv = (vec2(pixelPos) + vec2(0.5f)) / vec2(outputImageSize - ivec2(1));
		vec3 direction = latLongTexcoordToCartesian(uv);

		vec3 sampleSg = vec3(0.0f);
		for (u32 lobeIt = 0; lobeIt < lobeCount; ++lobeIt)
		{
			const SphericalGaussian& sg = lobes[lobeIt];
			sampleSg += sgEvaluate(sg, direction);
		}

		SphericalHarmonicsL2 directionSh = shEvaluateL2(direction);
		vec3 sampleSh = max(vec3(0.0f), shDot(shRadiance, directionSh));

		radianceSgImage.at(pixelPos) = vec4(sampleSg, 1.0f);
		radianceShImage.at(pixelPos) = vec4(sampleSh, 1.0f);

		averageRadianceSgSample += sampleSg.r;
		averageRadianceShSample += sampleSh.r;
	});

	radianceSgImage.writePng("radianceSG.png");
	radianceShImage.writePng("radianceSH.png");

	averageRadianceSgSample /= radianceSgImage.getPixelCount();
	averageRadianceShSample /= radianceShImage.getPixelCount();

	printf("Average SG radiance: %f\n", averageRadianceSgSample);
	printf("Average SH radiance: %f\n", averageRadianceShSample);

	///////////////////////////////////////////////////////////////
	// Generate irradiance image by convolving lighting with BRDF
	///////////////////////////////////////////////////////////////

	float averageSgIrradianceSample = 0.0f;
	float averageShIrradianceSample = 0.0f;
	Image irradianceSgImage(outputImageSize);
	Image irradianceShImage(outputImageSize);

	//SphericalGaussian brdf = sgCosineLobe();
	SphericalGaussian brdf;
	brdf.lambda = 6.5f; // Chosen arbitrarily through experimentation
	brdf.mu = vec3(sgFindMu(brdf.lambda, pi));

	irradianceSgImage.forPixels2D([&](vec4&, ivec2 pixelPos)
	{
		vec2 uv = (vec2(pixelPos) + vec2(0.5f)) / vec2(irradianceSgImage.getSize() - ivec2(1));
		vec3 direction = latLongTexcoordToCartesian(uv);

		brdf.p = direction;
		
		vec3 sampleSg = vec3(0.0f);
		for (u32 lobeIt = 0; lobeIt < lobeCount; ++lobeIt)
		{
			const SphericalGaussian& sg = lobes[lobeIt];
			sampleSg += sgDot(sg, brdf);
		}

		sampleSg /= pi;
		irradianceSgImage.at(pixelPos) = vec4(sampleSg, 1.0f);

		vec3 sampleSh = max(vec3(0.0f), shEvaluateDiffuseL2(shRadiance, direction) / pi);
		irradianceShImage.at(pixelPos) = vec4(sampleSh, 1.0f);

		averageSgIrradianceSample += sampleSg.r;
		averageShIrradianceSample += sampleSh.r;
	});

	averageSgIrradianceSample /= irradianceSgImage.getPixelCount();
	averageShIrradianceSample /= irradianceSgImage.getPixelCount();

	printf("Average SG irradiance: %f\n", averageSgIrradianceSample);
	printf("Average SH irradiance: %f\n", averageShIrradianceSample);

	irradianceSgImage.writePng("irradianceSG.png");
	irradianceShImage.writePng("irradianceSH.png");

	/////////////////////////////////////////////////////////
	// Generate reference convolved image using Monte Carlo
	/////////////////////////////////////////////////////////

	Concurrency::combinable<float> mcIrradianceSampleAccumulator;
	Image irradianceMcImage(outputImageSize);
	irradianceMcImage.parallelForPixels2D([&](vec4& pixel, ivec2 pixelPos)
	{
		vec2 uv = (vec2(pixelPos) + vec2(0.5f)) / vec2(outputImageSize - ivec2(1));
		vec3 direction = latLongTexcoordToCartesian(uv);

		mat3 basis = makeOrthogonalBasis(direction);

		vec3 sample = vec3(0.0f);
		const u32 mcSampleCount = 5000;
		for (u32 sampleIt = 0; sampleIt < mcSampleCount; ++sampleIt)
		{
			vec2 sampleUv = sampleHammersley(sampleIt, mcSampleCount);
			vec3 hemisphereDirection = sampleCosineHemisphere(sampleUv);
			vec3 sampleDirection = basis * hemisphereDirection;
			sample += getSample(sampleDirection);
		}

		sample /= mcSampleCount;

		pixel = vec4(sample, 1.0f);

		mcIrradianceSampleAccumulator.local() += sample.r;
	});

	irradianceMcImage.writePng("irradianceMC.png");

	float averageMcIrradianceSample = mcIrradianceSampleAccumulator.combine([](float a, float b){return a + b;});
	averageMcIrradianceSample /= irradianceMcImage.getPixelCount();
	printf("Average MC irradiance: %f\n", averageMcIrradianceSample);

	////////////////////////////////////////////////
	// Write all images into a single combined PNG
	////////////////////////////////////////////////

	Image combinedImage(outputImageSize.x*3, outputImageSize.y*2);

	combinedImage.paste(radianceImage, outputImageSize * ivec2(0,0));
	combinedImage.paste(radianceShImage, outputImageSize * ivec2(1, 0));
	combinedImage.paste(radianceSgImage, outputImageSize * ivec2(2, 0));

	combinedImage.paste(irradianceMcImage, outputImageSize * ivec2(0, 1));
	combinedImage.paste(irradianceShImage, outputImageSize * ivec2(1, 1));
	combinedImage.paste(irradianceSgImage, outputImageSize * ivec2(2, 1));
	
	combinedImage.writePng("combined.png");

	return 0;
}
