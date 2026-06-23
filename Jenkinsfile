pipeline {
    agent any

    environment {
        // Remplace par ton propre namespace DockerHub
        IMAGE_NAME = "TON_USER_DOCKERHUB/tp3-nodejs-api"
        IMAGE_TAG  = "${env.BUILD_NUMBER}"
        CONTAINER_NAME = "tp3-nodejs-api"
        HOST_PORT = "3000"
    }

    stages {
        stage('Checkout') {
            steps {
                checkout scm
            }
        }

        stage('Build Docker image') {
            steps {
                sh "docker build -t ${IMAGE_NAME}:${IMAGE_TAG} -t ${IMAGE_NAME}:latest ."
            }
        }

        stage('Push to DockerHub') {
            steps {
                withCredentials([usernamePassword(
                    credentialsId: 'dockerhub-creds',
                    usernameVariable: 'DOCKER_USER',
                    passwordVariable: 'DOCKER_PASS'
                )]) {
                    sh '''
                        echo "$DOCKER_PASS" | docker login -u "$DOCKER_USER" --password-stdin
                        docker push ${IMAGE_NAME}:${IMAGE_TAG}
                        docker push ${IMAGE_NAME}:latest
                    '''
                }
            }
        }

        stage('Deploy') {
            steps {
                sh '''
                    docker rm -f ${CONTAINER_NAME} || true
                    docker run -d \
                        --name ${CONTAINER_NAME} \
                        -p ${HOST_PORT}:3000 \
                        --restart unless-stopped \
                        ${IMAGE_NAME}:latest
                '''
            }
        }
    }

    post {
        success {
            echo "API NodeJS déployée sur le port ${HOST_PORT} ✅"
        }
        failure {
            echo "Échec du pipeline nodejs-api ❌"
        }
    }
}
