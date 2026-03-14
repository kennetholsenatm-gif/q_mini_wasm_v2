# WUI Frontend (Vite/React). Build from repo root: docker build -f wui/frontend/Dockerfile .
# Stage 1: build
FROM node:20-alpine AS builder
WORKDIR /app
COPY wui/frontend/package.json wui/frontend/package-lock.json* ./
RUN npm ci 2>/dev/null || npm install
COPY wui/frontend/ .
RUN npm run build

# Stage 2: serve with nginx
FROM nginx:alpine
COPY wui/frontend/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=builder /app/dist /usr/share/nginx/html
EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
