FROM python:3.13-slim
WORKDIR /app
COPY pyproject.toml README.md ./
COPY imd_local ./imd_local
RUN python -m pip install --no-cache-dir ".[system-trust,documents]"
COPY jobs.example.json ./
ENTRYPOINT ["python", "-m", "imd_local"]
CMD ["coverage"]
