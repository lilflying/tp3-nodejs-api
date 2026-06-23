# Image slim pour limiter la taille finale
FROM node:20-slim

WORKDIR /app

# Cache la couche d'install des dépendances séparément du code
COPY package*.json ./
RUN npm install --omit=dev

COPY server.js ./

EXPOSE 3000

CMD ["node", "server.js"]
