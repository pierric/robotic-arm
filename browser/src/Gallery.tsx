import React, { useState, useEffect, useMemo } from "react";
import Slider from "react-slick";

export default function Gallery() {
  const [page, setPage] = useState(0);
  const [elems, setElems] = useState<Array<any> | null>(null);

  const handleNextClick = () => {
    setElems(null);
    setPage(page + 1);
  };

  const handlePrevClick = () => {
    if(page > 0) {
      setElems(null);
      setPage(page - 1);
    }
  };

  const handleUpdatePage = (e: any) => {
    setPage(parseInt(e.target.value));
  }

  const pageSize = 8;

  const headers = useMemo(() => {
    return {
        "Content-Type": "application/json",
        "Authorization": "Basic YWRtaW46NDQ1ZGw2bWt6dHZibWN2a2ExcXQycTZiMjJ4bnN1MHdiZmpoamxkaHFtNnpuN2FkbA==",
      }
  }, []);

  const handleFirstClick = () => {
    setElems(null);
    setPage(0);
  }

  const handleLastClick = () => {
    const url = encodeURI("http://moon:8080/camera/_size");
    fetch(url, { headers })
      .then(resp => resp.json())
      .then(json => setPage(Math.floor(json._size / pageSize)));
  }

  useEffect(() => {
    const url = encodeURI(`http://localhost:8080/camera?page=${page+1}&pagesize=${pageSize}`);
    fetch(url, { headers })
      .then(resp => resp.json())
      .then(json => setElems(Array.from(json.entries())));
  }, [page, headers])

  const settings = {
    dots: true,
    centerMode: true,
    centerPadding: "60px",
    infinite: false,
    speed: 500,
    slidesToShow: 1,
    rows: 2,
    slidesPerRow: 4
  };

  return (
    <div className="slider-container">

      <button className="button" onClick={handleFirstClick}>
        First
      </button>

      <button className="button" onClick={handlePrevClick}>
        Prev
      </button>

      <button className="button" onClick={handleNextClick}>
        Next
      </button>

      <button className="button" onClick={handleLastClick}>
        Last
      </button>

      <div>{page}</div>

      <div><input onChange={handleUpdatePage}/></div>

      <div >
      {
        !elems ?
          <div>Loading...</div> :
        <Slider {...settings}>
        {
          elems.map(elem => {
            const [idx, value] = elem;
            const img = `data:image/png;base64,${value.image}`;
            return (
              <div key={idx}>
                <img src={img} alt="from camera"/>
                <span>{value.time_stamp}</span>
              </div>
            );
          })
        }
        </Slider>
      }
      </div>
    </div>
  );
}
