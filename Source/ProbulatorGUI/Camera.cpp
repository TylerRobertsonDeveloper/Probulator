#include "Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/transform.hpp>

Probulator::mat4 Camera::getProjectionMatrix() const
{
	return glm::perspective(m_fov, m_aspect, m_near, m_far);
}

mat4 Camera::getViewMatrix() const
{
	float x = -dot((vec3)m_orientation[0], (vec3)m_position);
	float y = -dot((vec3)m_orientation[1], (vec3)m_position);
	float z = -dot((vec3)m_orientation[2], (vec3)m_position);

	mat3 r = transpose(m_orientation);

	return mat4(
		vec4(r[0], 0.0),
		vec4(r[1], 0.0),
		vec4(r[2], 0.0),
		vec4(x, y, z, 1.0f)
	);
}

void Camera::rotate(float deltaAroundUp, float deltaAroundRight)
{
	mat3 rotUp = (mat3)glm::rotate(-deltaAroundUp, vec3(0.0f, 1.0f, 0.0f));
	mat3 rotRight = (mat3)glm::rotate(-deltaAroundRight, m_orientation[0]);
	m_orientation = rotUp * rotRight * m_orientation;
}

void Camera::move(const vec3& delta)
{
	m_position += delta.x * m_orientation[0];
	m_position += delta.y * m_orientation[1];
	m_position -= delta.z * m_orientation[2];
}

void CameraController::update(const InputState& input, Camera& camera)
{
	// TODO: orbit camera mode update

	float moveSpeed = m_moveSpeed * input.moveSpeedMultiplier;
	float rotateSpeed = m_rotateSpeed * input.rotateSpeedMultiplier;

	camera.rotate(
		input.rotateAroundUp * rotateSpeed,
		input.rotateAroundRight * rotateSpeed);

	vec3 moveDirection = vec3(
		input.moveRight,
		input.moveUp,
		input.moveForward);

	float moveDirectionLength = length(moveDirection);
	if (moveDirectionLength > 0.0f)
	{
		camera.move(moveDirection / moveDirectionLength * moveSpeed);
	}
}

const char* toString(CameraMode mode)
{
	switch (mode)
	{
	default:
		assert(false && "Unknown camera mode");
		return "Unknown";
	case CameraMode_FirstPerson:
		return "FirstPerson";
	case CameraMode_Orbit:
		return "Orbit";
	}
}

Camera CameraController::interpolate(const Camera& x, const Camera& y, 
	float positionAlpha, float orientationAlpha, float attributeAlpha)
{
	// TODO: orbit camera mode interpolation

	Camera result;

	result.m_aspect = mix(x.m_aspect, y.m_aspect, attributeAlpha);
	result.m_fov = mix(x.m_fov, y.m_fov, attributeAlpha);
	result.m_near = mix(x.m_near, y.m_near, attributeAlpha);
	result.m_far = mix(x.m_far, y.m_far, attributeAlpha);
	result.m_position = mix(x.m_position, y.m_position, positionAlpha);

	glm::quat qx = (glm::quat)x.m_orientation;
	glm::quat qy = (glm::quat)y.m_orientation;
	glm::quat qr = normalize(glm::slerp(qx, qy, orientationAlpha));
	
	result.m_orientation = (mat3)qr;

	return result;
}
