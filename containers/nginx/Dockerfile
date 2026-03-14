# Nginx reverse proxy (and optional WAF). Build from repo root.
# ModSecurity: use a Debian-based image with modsecurity-nginx if needed; Alpine placeholder for now.
FROM nginx:alpine
RUN rm -f /etc/nginx/conf.d/default.conf
COPY containers/nginx/nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
