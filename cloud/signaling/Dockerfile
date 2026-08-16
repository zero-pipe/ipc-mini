FROM node:20-alpine
WORKDIR /app
COPY package.json ./
RUN npm install --omit=dev
COPY server.js rooms.js protocol.js http.js ./
COPY public ./public
ENV PORT=8089
ENV SIGNALING_PATH=/ws
EXPOSE 8089
CMD ["node", "server.js"]
