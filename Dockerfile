FROM python:3.13-slim
WORKDIR /app
COPY pyproject.toml README.md LICENSE DATA_LICENSE.md ./
COPY imd_local ./imd_local
RUN python -m pip install --no-cache-dir ".[system-trust,documents]"
COPY jobs.example.json jobs.full.json jobs.incois.json ./
ENTRYPOINT ["python", "-m", "imd_local"]
CMD ["coverage"]
